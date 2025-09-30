#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <functional>

struct ScanEvent {
  String ip_address;
  uint32_t count;
  String tanggal; // YYYY-MM-DD
  String waktu;   // HH:mm:ss
};

class OfflineQueue {
public:
  bool begin(const char* path="/scan_queue.ndjson", size_t maxBytes=1024*1024);
  bool enqueue(const ScanEvent& e);
  // publishOne harus return true jika MQTT publish sukses
  size_t flush(std::function<bool(const ScanEvent&)> publishOne, size_t maxPerCall=200);
  size_t count() const;     // perkiraan jumlah baris
  size_t sizeBytes() const; // ukuran file
private:
  String _path; size_t _maxBytes=0;
  bool writeLine(const String& line);
  bool pruneIfOversize();   // buang baris tertua sampai <= _maxBytes
};