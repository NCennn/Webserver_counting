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
  if (!LittleFS.exists(_path)) return 0;
  File f = LittleFS.open(_path, "r"); if (!f) return 0;
  size_t c=0; while (f.findUntil("\n", "\0")) c++; f.close(); return c;
}

bool OfflineQueue::writeLine(const String& line){
  File f = LittleFS.open(_path, "a"); if (!f) return false;
  bool ok = (f.print(line) && f.print("\n")); f.close(); return ok;
}

bool OfflineQueue::pruneIfOversize(){
  size_t sz = sizeBytes();
  if (sz <= _maxBytes) return true;

  // buang ~25% teratas (baris tertua)
  File in = LittleFS.open(_path, "r"); if (!in) return false;
  File tmp = LittleFS.open(String(_path)+".tmp", "w"); if (!tmp) { in.close(); return false; }

  size_t target = sz - (_maxBytes*3/4); // jumlah byte yang ingin kita hapus
  size_t dropped=0;
  while (in.available() && dropped < target) {
    String line = in.readStringUntil('\n');
    dropped += line.length()+1;
  }
  // copy sisanya
  while (in.available()) {
    String line = in.readStringUntil('\n');
    tmp.print(line); tmp.print("\n");
  }
  in.close(); tmp.close();
  LittleFS.remove(_path);
  LittleFS.rename(String(_path)+".tmp", _path);
  return true;
}

bool OfflineQueue::enqueue(const ScanEvent& e){
  JsonDocument d;
  d["ip_address"]=e.ip_address; d["kode_barang"]=e.kode_barang;
  d["tanggal"]=e.tanggal; d["waktu"]=e.waktu;
  String line; serializeJson(d, line);
  if (!writeLine(line)) return false;
  return pruneIfOversize();
}

size_t OfflineQueue::flush(std::function<bool(const ScanEvent&)> publishOne, size_t maxPerCall){
  File in = LittleFS.open(_path, "r"); if (!in) return 0;
  File out = LittleFS.open(String(_path)+".tmp", "w"); if (!out) { in.close(); return 0; }

  size_t sent=0;
  while (in.available()) {
    String line = in.readStringUntil('\n'); line.trim();
    if (line.isEmpty()) continue;

    // parse
    JsonDocument d; DeserializationError e = deserializeJson(d, line);
    if (!e) {
      ScanEvent ev{ d["ip_address"].as<String>(), d["kode_barang"].as<String>(),
                    d["tanggal"].as<String>(), d["waktu"].as<String>() };
      bool ok=false;
      if (sent < maxPerCall) ok = publishOne(ev);
      if (ok) { sent++; continue; } // sukses → tidak ditulis ulang
    }
    // gagal parse / publish → tulis balik
    out.print(line); out.print("\n");
  }
  in.close(); out.close();
  LittleFS.remove(_path);
  LittleFS.rename(String(_path)+".tmp", _path);
  return sent;
}
