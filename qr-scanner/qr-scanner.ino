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
static constexpr int PIN_GM66_RX   = 1;   // RX dari ESP32S3 (terhubung ke TX GM66)
static constexpr int PIN_GM66_TX   = 2;   // TX dari ESP32S3 (terhubung ke RX GM66)
static constexpr int PIN_GM66_TRIG = -1;  // set ke pin digital jika modul GM66 memakai pin trigger (aktif LOW)

// SPI untuk W5500 — sesuaikan dengan papan Anda
static constexpr int PIN_W5500_CS   = 9;
static constexpr int PIN_W5500_RST  = 8;
static constexpr int PIN_SPI_SCK    = 12;
static constexpr int PIN_SPI_MISO   = 13;
static constexpr int PIN_SPI_MOSI   = 11;

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

// ====================== UTIL ================================
static String payloadFor(const ScanEvent& e) {
  // Bangun JSON tanpa alokasi besar
  JsonDocument d;
  d["ip_address"]  = e.ip_address;
  d["kode_barang"] = e.kode_barang;
  d["tanggal"]     = e.tanggal;
  d["waktu"]       = e.waktu;
  String out; serializeJson(d, out);
  return out;
}

static bool ensureMqttConnected() {
  // Pilih transport (Wi-Fi lebih dulu, jika tidak ada pakai Ethernet)
  if (!portal.selectClient(mqtt, wifiClient, ethClient)) return false;

  // Set server dari konfigurasi
  const AppConfig& cfg = portal.config();
  if (cfg.mqtt_host.isEmpty() || cfg.mqtt_port == 0) return false;
  mqtt.setServer(cfg.mqtt_host.c_str(), cfg.mqtt_port);

  if (mqtt.connected()) return true;

  // Client ID unik
  String cid = String("scanner-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok;
  if (cfg.mqtt_user.length()) ok = mqtt.connect(cid.c_str(), cfg.mqtt_user.c_str(), cfg.mqtt_pass.c_str());
  else                        ok = mqtt.connect(cid.c_str());
  return ok;
}

static bool publishEvent(const ScanEvent& e) {
  if (!ensureMqttConnected()) return false;
  const AppConfig& cfg = portal.config();
  if (cfg.mqtt_topic.isEmpty()) return false;

  String js = payloadFor(e);
  // PubSubClient max payload default 256B; naikkan via setBufferSize jika diperlukan
  #ifndef MQTT_MAX_PACKET_SIZE
  #define MQTT_MAX_PACKET_SIZE 256
  #endif
  if (js.length() > MQTT_MAX_PACKET_SIZE - 16) {
    // Buffer default 256; jika lebih besar, coba setBufferSize
    mqtt.setBufferSize(js.length() + 32);
  }
  return mqtt.publish(cfg.mqtt_topic.c_str(), js.c_str());
}

static size_t flushQueueLimited(size_t maxItems = 200) {
  return queue.flush([](const ScanEvent& ev){
    return publishEvent(ev);
  }, maxItems);
}

static ScanEvent makeEventFromCode(const String& code) {
  ScanEvent e;
  e.ip_address = portal.activeIP();
  e.kode_barang = code;

  String tgl, wkt;
  if (rtc.isValid()) {
    rtc.nowLocal(tgl, wkt);
  } else {
    // Fallback: gunakan strftime agar panjang string aman untuk buffer
    struct tm ti;
    if (getLocalTime(&ti, 500)) {
      char b1[11]; // YYYY-MM-DD (10) + NUL
      char b2[9];  // HH:MM:SS (8)  + NUL
      size_t n1 = strftime(b1, sizeof(b1), "%Y-%m-%d", &ti);
      size_t n2 = strftime(b2, sizeof(b2), "%H:%M:%S", &ti);
      tgl = n1 ? b1 : "1970-01-01";
      wkt = n2 ? b2 : "00:00:00";
    } else {
      tgl = "1970-01-01";
      wkt = "00:00:00";
    }
  }
  e.tanggal = tgl;
  e.waktu   = wkt;
  return e;
}

// =================== CALLBACK SCAN ===========================
static void onScanCb(const String& kode) {
  ScanEvent e = makeEventFromCode(kode);
  if (publishEvent(e)) {
    Serial.printf("[MQTT] Publish OK: %s\n", kode.c_str());
  } else {
    bool ok = queue.enqueue(e);
    Serial.printf("[QUEUE] Enqueue %s (%s)\n", ok?"OK":"FAILED", kode.c_str());
  }
}

// =================== SETUP / LOOP ============================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] Barcode & Counter — QR Scanner");

  // FS & antrian
  LittleFS.begin(true);
  queue.begin(QUEUE_FILE, 5*1024*1024);

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

  // RTC DS3231
  rtc.begin(); // gunakan default pin I2C dari board; ubah di RTCClockDS3231.h jika perlu
  if (!rtc.isValid()) {
    // Coba sync NTP jika ada internet (agar DS3231 terset untuk ke depannya)
    rtc.syncFromNTPAndSetRTC(7000);
  }

  // Scanner GM66 di Serial1 (ubah pin sesuai atas)
  scanner.begin(Serial1, PIN_GM66_RX, PIN_GM66_TX, 9600, PIN_GM66_TRIG);
  scanner.setDebounceMs(800);
  scanner.onScan(onScanCb);

  // MQTT client basic callbacks (opsional)
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
  if (millis() - lastMqttAttempt > 1000) {
    lastMqttAttempt = millis();
    ensureMqttConnected();
  }
  if (mqtt.connected()) mqtt.loop();

  // Coba flush antrian tiap 2 detik saat online
  if (mqtt.connected() && (millis() - lastFlushCheck > 2000)) {
    lastFlushCheck = millis();
    size_t n = flushQueueLimited(100);
    if (n) Serial.printf("[QUEUE] Flushed %u items\n", (unsigned)n);
  }

  delay(2);
}
