#pragma once
#include <Arduino.h>
#include <WiFi.h>
// #include <ESPmDNS.h>
#include <MDNS_Generic.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

struct AppConfig {
  String wifi_ssid;
  String wifi_pass;
  String eth_ip;
  String eth_gateway;
  String eth_subnet;
  String mqtt_host;
  uint16_t mqtt_port = 1883;
  String mqtt_user;
  String mqtt_pass;
  String mqtt_topic;
};

class DualNICPortal {
public:
  struct Pins { int w5500_cs; int w5500_rst; };
  struct NetInfo { bool wifi; bool eth; bool ap; IPAddress ip; String ipStr; };

  // Return true if handled; set contentType, code, out
  using ExtraApiHandler = std::function<bool(
    const String& path, const String& method, const String& body,
    const String& ctx, String& contentType, int& code, String& out)>;

  // Let caller augment /api/status JSON (e.g., add queue stats)
  using StatusAugmenter = std::function<void(JsonDocument& root)>;

  DualNICPortal(const Pins& pins,
                const char* mdnsHost = "barcode",
                const char* configPath = "/config.json");

  // boot the portal: load config, AP/STA, Ethernet, HTTP routes
  void begin();

  // call in loop(): ethernet HTTP, AP timer, Wi-Fi watchdog
  void loop();
  void scheduleRestart(){ _pendingRestart = true; }


  // choose WiFi/Ethernet for MQTT client; returns true if a link exists
  bool selectClient(PubSubClient& mqtt, WiFiClient& wifiClientRef, EthernetClient& ethClientRef);

  // info helpers
  NetInfo current() const;
  String activeIP() const;
  bool ethernetLinkUp() const;

  // config access
  AppConfig& config();

  // hooks
  void setExtraApiHandler(ExtraApiHandler fn);
  void setStatusAugmenter(StatusAugmenter fn);

private:
  // pins & cfg
  Pins _pins;
  String _mdnsHost;
  String _configPath;

  // servers & clients
  AsyncWebServer _server{80};
  EthernetServer _ethServer{80};
  WiFiClient _wifiClient;
  EthernetClient _ethClient;
  PubSubClient _mqttTest; // internal for /api/mqtt/test

  // state
  AppConfig _cfg;
  bool _mdnsStarted = false;
  String _apSSID;
  uint32_t _apOffAt = 0;
  const uint32_t _AP_DEFAULT_MINUTES = 10;

  // wifi watchdog
  bool _wifiWatchActive = false;
  uint32_t _wifiWatchStart = 0;
  uint32_t _wifiScanAt = 0;
  const uint32_t _WIFI_LOST_AP_DELAY_MS = 30000;

  // hooks
  ExtraApiHandler _extraHandler = nullptr;
  StatusAugmenter _statusAugmenter = nullptr;

  // ==== internals ====
  void loadConfig();
  bool saveConfig();
  void setupAPIfNoCred();
  bool connectWiFiBlocking(uint32_t timeoutMs = 30000);
  void disconnectWiFi();
  void startMDNSIfNeeded();
  void apEnableForMinutes(uint32_t minutes);
  void apDisable();
  void wifiWatchdogLoop();

  void ethernetResetPulse();
  bool ethernetBeginStatic();
  void setupAsyncRoutes();
  void serveEthernet();

  // api plumbing
  String apiHandler(const String& path, const String& method, const String& body,
                    const String& ctx, String& contentType, int& code);
  String jsonStatus(const String& ctx);
  String jsonOk(const String& msg = "ok");
  String jsonErr(const String& msg);

  // mqtt test endpoint helper
  bool mqttTestConnectivity(uint32_t timeoutMs = 30000);
  bool _pendingRestart = false;
};
