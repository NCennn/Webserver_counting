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
#include "BarcodeScannerGM66.h"
#include "OfflineQueue.h"
#include "RTCClockDS3231.h"

// ====================== KONFIGURASI PIN ======================
// SESUAIKAN dengan wiring Anda! Nilai di bawah hanyalah contoh.
static constexpr int PIN_GM66_RX   = 44;   // RX dari ESP32S3 (terhubung ke TX GM66)
static constexpr int PIN_GM66_TX   = 43;   // TX dari ESP32S3 (terhubung ke RX GM66)
static constexpr int PIN_GM66_TRIG = -1;  // set ke pin digital jika modul GM66 memakai pin trigger (aktif LOW)
static const size_t QUEUE_MAX_BYTES = 512 * 1024;

// SPI untuk W5500 — sesuaikan dengan papan Anda
static constexpr int PIN_W5500_CS   = 4;
static constexpr int PIN_W5500_RST  = 8;
static constexpr int PIN_SPI_SCK    = 12;
static constexpr int PIN_SPI_MISO   = 13;
static constexpr int PIN_SPI_MOSI   = 11;

static const int I2C_SDA = 5;
static const int I2C_SCL = 6;

// mDNS hostname
static const char* MDNS_HOST = "barcode";

// Path file antrian offline
static const char* QUEUE_FILE = "/scan_queue.ndjson";

// ====================== OBJEK GLOBAL =========================
DualNICPortal portal({PIN_W5500_CS, PIN_W5500_RST, PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI}, MDNS_HOST);
WiFiClient     wifiClient;
EthernetClient ethClient;
PubSubClient   mqtt;
BarcodeScannerGM66 scanner;
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
  d["kode_barang"] = e.kode_barang;
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
  Serial.println("\n[BOOT] Barcode & Counter — QR Scanner");

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
  if (!rtc.begin(I2C_SDA, I2C_SCL)) Serial.println("RTC init failed");
  if (!rtc.isValid()) Serial.println("RTC not valid – set via NTP when Wi‑Fi up");

  // Scanner GM66 di Serial1 (ubah pin sesuai atas)
  scanner.begin(Serial1, PIN_GM66_RX, PIN_GM66_TX, 9600, PIN_GM66_TRIG);
  scanner.setDebounceMs(600);
  scanner.onScan([&](const String& kode){
    String tgl, jam; rtc.nowLocal(tgl, jam);
    ScanEvent ev{ activeIP(), kode, tgl, jam };
    bool sent = false;
    if (ensureMqttConnected()) sent = publishEvent(ev);
    if (!sent) {
      queue.enqueue(ev);
      Serial.printf("[QUEUE] Enqueued: %s\n", kode.c_str());
    } else {
      Serial.printf("[MQTT] Sent: %s\n", kode.c_str());
    }
  });

  // MQTT client basic callbacks (opsional)
  ensureMqttConnected();
  mqtt.setCallback([](char*, uint8_t*, unsigned int){});

  Serial.printf("[INFO] Web UI: http://%s.local/\n", MDNS_HOST);
  Serial.println("[READY] Scan kode untuk menguji...");
}

uint32_t lastMqttAttempt = 0;
uint32_t lastFlushCheck  = 0;

void loop() {
  portal.loop();     // wajib dipanggil

  // Jalankan loop scanner
  scanner.loop();

  // Jaga koneksi MQTT & lakukan flush periodik
  // if (millis() - lastMqttAttempt > 1000) {
  //   lastMqttAttempt = millis();
  //   ensureMqttConnected();
  // }
  if (mqtt.connected()) mqtt.loop();

  // Coba flush antrian tiap 2 detik saat online
  if (mqtt.connected() && (millis() - lastFlushCheck > 2000)) {
    lastFlushCheck = millis();
    size_t n = flushQueueLimited(100);
    if (n) Serial.printf("[QUEUE] Flushed %u items\n", (unsigned)n);
  }

  delay(2);
}
