#pragma once
#include <Arduino.h>
#include <functional>

class BarcodeScannerGM66 {
public:
  using Callback = std::function<void(const String& kode)>;

  void begin(HardwareSerial& ser, int rxPin, int txPin, uint32_t baud=9600, int trigPin=-1);
  void onScan(Callback cb) { _cb = cb; }
  void loop();

  void setDebounceMs(uint32_t ms) { _debounceMs = ms; }
  void triggerOnce(uint16_t lowMs=50); // aktifkan kalau trigPin terpasang

private:
  HardwareSerial* _s = nullptr;
  int _trig=-1;
  String _buf;
  Callback _cb;
  String _last; uint32_t _lastMs=0, _debounceMs=800;

  void handleLine(const String& line);
};
