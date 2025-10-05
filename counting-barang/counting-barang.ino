/*
  qr-scanner.ino — ESP32S3 XIAO + W5500 + GM66
  Fitur:
    - Wi-Fi prioritas, fallback Ethernet (W5500).
    - Konfigurasi lewat DualNICPortal (Wi-Fi/Ethernet + mDNS).
    - MQTT publish payload JSON: {ip_address, kode_barang, tanggal, waktu}.
    - Antrian offline (LittleFS NDJSON); auto-flush saat online.
    - DS3231 untuk tanggal/waktu (WIB). Fallback NTP jika tersedia.
    - Endpoint extra:
        POST /api/queue/flush  -> {flushed: <n>}
    - Status UI menampilkan jumlah item & size antrian.
  Catatan:
    - Ubah pin sesuai wiring Anda di bagian "=== KONFIGURASI PIN ===".
    - Pastikan board: Seeed XIAO ESP32S3, core ESP32 >= 2.0.14.
    - Library: ESPAsyncWebServer, Ethernet, PubSubClient, ArduinoJson, RTClib.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

#include "DualNICPortal.h"
#include "OfflineQueue.h"
#include "RTCClockDS3231.h"

// ====================== KONFIGURASI PIN ======================
// SESUAIKAN dengan wiring Anda! Nilai di bawah hanyalah contoh.
#define SENSOR_PIN 2
#define LED_PIN_STATUS 43
#define LED_PIN_TRIG 1
#define FAN_PIN 3

static const size_t QUEUE_MAX_BYTES = 512 * 1024;
uint32_t lastLEDBlink = 0;
bool ledBlinkState = false;


// SPI untuk W5500 — sesuaikan dengan papan Anda
#define PIN_W5500_CS     4
#define W5500_RST   -1

float temp = 0;
uint32_t itemCount = 0;
int lastSensorState = HIGH;


// mDNS hostname
static const char* MDNS_HOST = "counter";

// Path file antrian offline
static const char* QUEUE_FILE = "/scan_queue.ndjson";

// ====================== OBJEK GLOBAL =========================
DualNICPortal portal({PIN_W5500_CS, W5500_RST}, MDNS_HOST);
WiFiClient     wifiClient;
EthernetClient ethClient;
PubSubClient   mqtt;
OfflineQueue   queue;
RTCClockDS3231 rtc;

String activeIP(){
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
#ifdef ETHERNET_H
  if (Ethernet.linkStatus() == LinkON) return Ethernet.localIP().toString();
#endif
  return String("0.0.0.0");
}

static bool ensureMqttConnected() {
  // Pilih transport (Wi-Fi lebih dulu, jika tidak ada pakai Ethernet)
  if (!portal.selectClient(mqtt, wifiClient, ethClient)) return false;

  // Set server dari konfigurasi
  const AppConfig& cfg = portal.config();
  if (cfg.mqtt_host.isEmpty() || cfg.mqtt_port == 0) return false;
  mqtt.setServer(cfg.mqtt_host.c_str(), cfg.mqtt_port);

  Serial.print("Menghubungkan ke MQTT ");
  Serial.print(cfg.mqtt_host);
  Serial.print(":");
  Serial.println(cfg.mqtt_port);

  if (mqtt.connected()) return true;

  // Client ID unik
  String cid = String("scanner-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok;
  if (cfg.mqtt_user.length()) ok = mqtt.connect(cid.c_str(), cfg.mqtt_user.c_str(), cfg.mqtt_pass.c_str());
  else                        ok = mqtt.connect(cid.c_str());

  if(ok) Serial.println("[MQTT] Connected");
  else Serial.printf("[MQTT] Connection Failed, rc= %d", mqtt.state());

  return ok;
}


bool publishEvent(const ScanEvent& e){
  const AppConfig& cfg = portal.config();
  JsonDocument d;
  d["ip_address"]  = e.ip_address;
  d["count"] = e.count;
  d["tanggal"]     = e.tanggal;
  d["waktu"]       = e.waktu;
  String buf; 
  serializeJson(d, buf);
  return mqtt.publish(cfg.mqtt_topic.c_str(), buf.c_str(), true);
}

static size_t flushQueueLimited(size_t maxItems = 200) {
  return queue.flush([](const ScanEvent& ev){
    return publishEvent(ev);
  }, maxItems);
}


// =================== SETUP / LOOP ============================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] Counter is Ready");
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_PIN_TRIG, OUTPUT);
  pinMode(LED_PIN_STATUS, OUTPUT);
  queue.begin(QUEUE_FILE, QUEUE_MAX_BYTES);

  // Portal jaringan (Wi-Fi/Ethernet + UI)
  portal.setStatusAugmenter([](JsonDocument& root){
    // Tambahkan statistik antrian di /api/status -> ui
    root["queue"]["count"] = queue.count();
    root["queue"]["bytes"] = queue.sizeBytes();
  });
  portal.setExtraApiHandler([](const String& path, const String& method, const String& body,
                               const String& ctx, String& contentType, int& code, String& out)->bool {
    if (path == "/api/queue/flush" && method == "POST") {
      size_t n = flushQueueLimited(500);
      JsonDocument d; d["flushed"] = n; serializeJson(d, out);
      contentType = "application/json"; code = 200; return true;
    }
    return false;
  });

  portal.begin();

  // RTC
  if (!rtc.begin()) Serial.println("RTC init failed");
  if (!rtc.isValid()) Serial.println("RTC not valid – set via NTP when Wi‑Fi up");
  Serial.println(rtc.syncFromNTPAndSetRTC(10000) ? "[RTC] Success Sync Time": "[RTC] Failed Sync Time" );

  // MQTT client basic callbacks (opsional)
  ensureMqttConnected();
  mqtt.setCallback([](char*, uint8_t*, unsigned int){});

  Serial.printf("[INFO] Web UI: http://%s.local/\n", MDNS_HOST);
  Serial.println("[READY] Scan kode untuk menguji...");
}

uint32_t lastFlushCheck  = 0;

void loop() {
  portal.loop();     // wajib dipanggil
  checkSensor();
  temp = rtc.getTemp();
  Serial.println(temp);

  if (temp >= 50 ){
    analogWrite(FAN_PIN, 255);
    // Serial.println("FAN ON");
    delay(1000);
  }else if(temp <= 30){
    analogWrite(FAN_PIN, 0);
    // Serial.println("FAN OFF");
    delay(1000);
  }

  if (!mqtt.loop()) {
    if (millis() - lastLEDBlink >= 1000) {
      lastLEDBlink = millis();
      ledBlinkState = !ledBlinkState;
      digitalWrite(LED_PIN_STATUS, ledBlinkState ? HIGH : LOW);
    }
  } else if(mqtt.connected() || WiFi.status() == WL_CONNECTED){
    digitalWrite(LED_PIN_STATUS, HIGH);
    // mqtt.loop(); // tetap jalankan loop MQTT saat connected
  }
  // Coba flush antrian tiap 2 detik saat online
  if (mqtt.connected() && (millis() - lastFlushCheck > 2000)) {
    lastFlushCheck = millis();
    size_t n = flushQueueLimited(100);
    if (n) Serial.printf("[QUEUE] Flushed %u items\n", (unsigned)n);
  }

  delay(2);
}

 void checkSensor() {
    int sensorState = digitalRead(SENSOR_PIN);

    // Serial.println(sensorState);
    digitalWrite(LED_PIN_TRIG, sensorState);

    
    if (lastSensorState == LOW && sensorState == HIGH) {
      itemCount++;
      String tgl, jam; rtc.nowLocal(tgl, jam);
      ScanEvent ev{ activeIP(), itemCount, tgl, jam };
      bool sent = false;
      if (mqtt.connected()) sent = publishEvent(ev);
      if (!sent) {
        queue.enqueue(ev);
        Serial.printf("[QUEUE] Enqueued: %lu\n", (unsigned long)itemCount);
      } else {
        Serial.printf("[MQTT] Sent: %lu\n", (unsigned long) itemCount);
      }
      delay(300); // Debounce
    } 
    lastSensorState =  sensorState;
  }