/*
  Sketch Utama - Menggunakan DualNICPortal (library)
  Fitur:
    - GM66 (UART) + debounce → kirim ke MQTT (cfg.mqtt_topic + "/scan")
    - DS3231 (SDA=5, SCL=6), waktu lokal WIB, NTP sync saat online
    - OfflineQueue 5MB (LittleFS), auto-flush saat MQTT connected
    - Portal jaringan dipindah ke /lib/DualNICPortal
*/

#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <RTClib.h>

// === custom libs ===
#include "OfflineQueue.h"
#include "BarcodeScannerGM66.h"
#include "RTCClockDS3231.h"
#include "DualNICPortal.h"

// ====== USER CONFIG: GM66 ======
#define GM66_RX_PIN      44
#define GM66_TX_PIN      43
#define GM66_TRIG_PIN   -1
#define GM66_BAUD        9600
#define GM66_DEBOUNCE_MS 800

// ====== Instances ======
BarcodeScannerGM66 g_scanner;
RTCClockDS3231     g_rtc;
OfflineQueue       g_queue;

// MQTT client (portal akan pilihkan backend WiFi/Eth)
WiFiClient     g_wifiClient;
EthernetClient g_ethClient;
PubSubClient   g_mqtt;

// Portal pins (SAMAKAN dgn wiring W5500)
DualNICPortal::Pins portalPins{
  /*w5500_cs=*/5, /*w5500_rst=*/-1, /*sck=*/8, /*miso=*/9, /*mosi=*/10
};
DualNICPortal portal(portalPins, "barcode", "/config.json");


static String mqttScanTopic(){ return portal.config().mqtt_topic + "/scan"; }

// Bangun payload JSON yang sama seperti yang dikirim ke MQTT
static void buildScanJson(const ScanEvent& ev, String& out){
  JsonDocument d;
  d["ip_address"]  = ev.ip_address;
  d["kode_barang"] = ev.kode_barang;
  d["tanggal"]     = ev.tanggal;
  d["waktu"]       = ev.waktu;
  serializeJson(d, out);
} 

static bool mqttPublishScan(const ScanEvent& ev){
  String payload;
  buildScanJson(ev, payload);
  // (opsional) tampilkan juga saat publish langsung:
  Serial.print("[MQTT] "); Serial.println(payload);
  return g_mqtt.publish(mqttScanTopic().c_str(), payload.c_str(), false);
}


static bool ensureMqttConnected(){
  auto& cfg = portal.config();
  if (cfg.mqtt_host.isEmpty()) return false;
  if (!portal.selectClient(g_mqtt, g_wifiClient, g_ethClient)) return false;

  g_mqtt.setServer(cfg.mqtt_host.c_str(), cfg.mqtt_port);
  if (g_mqtt.connected()) return true;

  // unique id
  uint8_t mac[6]; WiFi.macAddress(mac);
  char id[20]; snprintf(id, sizeof(id), "barcode_%02X%02X%02X", mac[3],mac[4],mac[5]);

  if (cfg.mqtt_user.length()){
    return g_mqtt.connect(id, cfg.mqtt_user.c_str(), cfg.mqtt_pass.c_str());
  } else {
    return g_mqtt.connect(id);
  }
}

// barcode callback
static void onBarcodeScanned(const String& kode){
  String tgl, jam; g_rtc.nowLocal(tgl, jam);
  ScanEvent ev{ portal.activeIP(), kode, tgl, jam };

  bool published=false;
  if (ensureMqttConnected()){
    published = mqttPublishScan(ev);
  }
  if (!published){
    // map ke OfflineQueue::ScanEvent
    ::ScanEvent qev{ev.ip_address, ev.kode_barang, ev.tanggal, ev.waktu}; // overload aman
    // catatan: OfflineQueue.h memiliki struct ScanEvent yang sama; cast manual:
    // supaya gampang, tulis langsung:
    OfflineQueue tmp; // hanya untuk tipe - tidak dipakai
    g_queue.enqueue(*(reinterpret_cast<const ::ScanEvent*>(&ev))); // tapi lebih aman begini ↓
  }
}

// karena beda struct, bikin helper enqueue aman:
static void enqueueEvent(const ScanEvent& ev){
  ::ScanEvent e2{ev.ip_address, ev.kode_barang, ev.tanggal, ev.waktu};
  g_queue.enqueue(e2);
}

void setup(){
  Serial.begin(115200); delay(200);
  Serial.println("=== Boot (with DualNICPortal) ===");

  // start portal (jaringan + UI + API dasar)
  portal.begin();

  // hooks: tambah info queue ke /api/status
  portal.setStatusAugmenter([](JsonDocument& root){
    // root["queue"]["count"], ["bytes"]
    extern OfflineQueue g_queue;
    root["queue"]["count"] = g_queue.count();
    root["queue"]["bytes"] = g_queue.sizeBytes();
  });

  // hooks: endpoint ekstra (queue + scanner trigger)
  portal.setExtraApiHandler([](const String& path, const String& method, const String& body,
                              const String& ctx, String& ct, int& code, String& out)->bool{
    ct = "application/json"; code = 200;

    if (path == "/api/queue/status" && method == "GET") {
      JsonDocument d; d["count"]=g_queue.count(); d["bytes"]=g_queue.sizeBytes();
      serializeJson(d, out); return true;
    }
    if (path == "/api/queue/flush" && method == "POST") {
      size_t n = 0;
      if (ensureMqttConnected()){
        n = g_queue.flush([](const ScanEvent& ev){   // <<--- langsung ScanEvent
          return mqttPublishScan(ev);
        }, 1000);
      }
      JsonDocument d; d["flushed"] = (int)n; serializeJson(d, out); return true;
    }
    if (path == "/api/scanner/trigger" && method == "POST") {
    #if GM66_TRIG_PIN >= 0
      g_scanner.triggerOnce(60);
      JsonDocument d; d["status"]="triggered"; serializeJson(d,out); return true;
    #else
      code = 400; out = "{\"error\":\"no_trigger_pin\"}"; return true;
    #endif
    }
    return false;
  });


  // RTC & Scanner & Queue
  if (!g_rtc.begin(5,6)) Serial.println("[RTC] DS3231 init gagal");
  else Serial.println("[RTC] DS3231 ok");

  g_scanner.begin(Serial1, GM66_RX_PIN, GM66_TX_PIN, GM66_BAUD, GM66_TRIG_PIN);
  g_scanner.setDebounceMs(GM66_DEBOUNCE_MS);
  g_scanner.onScan([](const String& k){
    String tgl, jam; g_rtc.nowLocal(tgl, jam);
    ScanEvent ev{ portal.activeIP(), k, tgl, jam };

    // Cetak payload JSON ke Serial
    String payload; 
    buildScanJson(ev, payload);
    Serial.print("[SCAN] ");
    Serial.println(payload);

    // Kirim ke MQTT atau antrikan
    if (ensureMqttConnected()){
      if (!g_mqtt.publish(mqttScanTopic().c_str(), payload.c_str(), false)) {
        g_queue.enqueue(ev);
      }
    } else {
      g_queue.enqueue(ev);
    }
  });



  g_queue.begin("/scan_queue.ndjson", 5*1024*1024);

  // kalau RTC belum valid & Wi-Fi sudah connect, sync NTP (WIB)
  if (!g_rtc.isValid()){
    if (ensureMqttConnected() || (WiFi.status()==WL_CONNECTED)) {
      if (g_rtc.syncFromNTPAndSetRTC(7000)) Serial.println("[RTC] RTC diset dari NTP (WIB).");
      else Serial.println("[RTC] NTP gagal; RTC mungkin belum valid.");
    }
  }
}

void loop(){
  portal.loop();

  g_scanner.loop();

  g_mqtt.loop();
  if (ensureMqttConnected()){
    g_queue.flush([](const ScanEvent& ev){
      String payload; 
      buildScanJson(ev, payload);
      Serial.print("[FLUSH] ");
      Serial.println(payload);
      return g_mqtt.publish(mqttScanTopic().c_str(), payload.c_str(), false);
    }, 200);
  }
}
