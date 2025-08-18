#include "OfflineQueue.h"
#include <ArduinoJson.h>

bool OfflineQueue::begin(const char* path, size_t maxBytes){
  _path = path; _maxBytes = maxBytes;
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists(_path)) { File f = LittleFS.open(_path, "w"); if (f) f.close(); }
  return true;
}

size_t OfflineQueue::sizeBytes() const {
  if (!LittleFS.exists(_path)) return 0;
  File f = LittleFS.open(_path, "r"); if (!f) return 0;
  size_t n = f.size(); f.close(); return n;
}

size_t OfflineQueue::count() const {
  File f = LittleFS.open(_path, "r"); if (!f) return 0; size_t n=0;
  while (f.available()) { if (f.read()=='\n') n++; }
  f.close(); return n;
}

bool OfflineQueue::writeLine(const String& line){
  File f = LittleFS.open(_path, "a"); if (!f) return false;
  bool ok = (f.print(line) && f.print('\n'));
  f.close(); return ok;
}

bool OfflineQueue::pruneIfOversize(){
  size_t sz = sizeBytes();
  if (sz <= _maxBytes) return true;
  // buang 20% terdepan
  size_t target = (size_t)(sz * 0.8f);
  File src = LittleFS.open(_path, "r"); if (!src) return false;
  File tmp = LittleFS.open(String(_path)+".tmp", "w"); if (!tmp){ src.close(); return false; }
  // skip awal sampai ukuran ~target
  size_t skipped=0; String line;
  while (src.available() && skipped < (sz - target)){
    line = src.readStringUntil('\n'); skipped += line.length()+1;
  }
  // copy sisanya
  while (src.available()){
    line = src.readStringUntil('\n'); tmp.print(line); tmp.print('\n');
  }
  src.close(); tmp.close();
  LittleFS.remove(_path);
  LittleFS.rename(String(_path)+".tmp", _path);
  return true;
}

bool OfflineQueue::enqueue(const ScanEvent& e){
  JsonDocument d;
  d["ip_address"]  = e.ip_address;
  d["kode_barang"] = e.kode_barang;
  d["tanggal"]     = e.tanggal;
  d["waktu"]       = e.waktu;
  String line; serializeJson(d, line);
  if (!writeLine(line)) return false;
  pruneIfOversize();
  return true;
}

size_t OfflineQueue::flush(std::function<bool(const ScanEvent&)> publishOne, size_t maxPerCall){
  File src = LittleFS.open(_path, "r"); if (!src) return 0;
  File tmp = LittleFS.open(String(_path)+".tmp", "w"); if (!tmp){ src.close(); return 0; }

  size_t flushed=0; String line;
  while (src.available()){
    line = src.readStringUntil('\n'); line.trim(); if (!line.length()) continue;

    // parse JSON line
    JsonDocument d; DeserializationError err = deserializeJson(d, line);
    bool ok = false;
    if (!err){
      ScanEvent e { (const char*)d["ip_address"], (const char*)d["kode_barang"], (const char*)d["tanggal"], (const char*)d["waktu"] };
      ok = publishOne(e);
    }

    if (ok) {
      flushed++;
      if (flushed >= maxPerCall) {
        // salin sisa baris apa adanya
        while (src.available()) { String rest = src.readStringUntil('\n'); tmp.print(rest); tmp.print('\n'); }
        break;
      }
    } else {
      // tulis kembali yang gagal
      tmp.print(line); tmp.print('\n');
    }
  }
  src.close(); tmp.close();
  LittleFS.remove(_path);
  LittleFS.rename(String(_path)+".tmp", _path);
  return flushed;
}