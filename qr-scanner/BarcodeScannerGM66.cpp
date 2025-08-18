#include "BarcodeScannerGM66.h"

void BarcodeScannerGM66::begin(HardwareSerial& ser, int rxPin, int txPin, uint32_t baud, int trigPin){
  _s = &ser;
  _s->begin(baud, SERIAL_8N1, rxPin, txPin);
  _trig = trigPin;
  if (_trig >= 0){ pinMode(_trig, OUTPUT); digitalWrite(_trig, HIGH); } // HIGH idle
  _buf.reserve(128);
}

void BarcodeScannerGM66::triggerOnce(uint16_t lowMs){
  if (_trig < 0) return;
  digitalWrite(_trig, LOW);
  delay(lowMs);
  digitalWrite(_trig, HIGH);
}

void BarcodeScannerGM66::handleLine(const String& line){
  String s = line; s.trim();
  if (!s.length()) return;
  const uint32_t now = millis();
  if (s == _last && (now - _lastMs) < _debounceMs) return; // debounce duplikat cepat
  _last = s; _lastMs = now;
  if (_cb) _cb(s);
}

void BarcodeScannerGM66::loop(){
  if (!_s) return;
  while (_s->available()){
    char c = (char)_s->read();
    if (c == '\r' || c == '\n'){
      if (_buf.length()) { handleLine(_buf); _buf = ""; }
    } else {
      if (_buf.length() < 200) _buf += c;
    }
  }
}