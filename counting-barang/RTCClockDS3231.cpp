#include "RTCClockDS3231.h"
#include <WiFi.h>
#include <time.h> 

bool RTCClockDS3231::begin(){
  if (!_rtc.begin()) return false;
  return true;
}

bool RTCClockDS3231::isValid(){
  if (_rtc.lostPower()) return false;
  DateTime n = _rtc.now();
  return (n.year() >= 2020 && n.year() <= 2099);
}

void RTCClockDS3231::nowLocal(String& tanggal, String& waktu){
  DateTime n = _rtc.now();
  // Anggap RTC sudah berisi WIB (paling aman untuk offline). Jika ingin RTC diset UTC, ubah offset di sini.
  char buf1[16], buf2[16];
  snprintf(buf1, sizeof(buf1), "%04d-%02d-%02d", n.year(), n.month(), n.day());
  snprintf(buf2, sizeof(buf2), "%02d:%02d:%02d", n.hour(), n.minute(), n.second());
  tanggal = buf1; waktu = buf2;
}

bool RTCClockDS3231::syncFromNTPAndSetRTC(uint32_t timeoutMs){
  if (WiFi.status() != WL_CONNECTED) return false;
  configTime(7*3600, 0, "pool.ntp.org", "time.nist.gov"); // UTC+7
  const uint32_t t0 = millis();
  struct tm info{};
  while (millis()-t0 < timeoutMs){ if (getLocalTime(&info, 200)) break; }
  if (info.tm_year == 0) return false;
  DateTime dt(info.tm_year + 1900, info.tm_mon+1, info.tm_mday, info.tm_hour, info.tm_min, info.tm_sec);
  _rtc.adjust(dt);
  return true;
}

float RTCClockDS3231::getTemp(){
  return _rtc.getTemperature();
}