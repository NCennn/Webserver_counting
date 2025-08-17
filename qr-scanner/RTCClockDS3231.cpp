#include "RTCClockDS3231.h"

bool RTCClockDS3231::begin(int sda, int scl){
  Wire.begin(sda, scl);
  if (!_rtc.begin()) return false;
  return true;
}

bool RTCClockDS3231::isValid(){
  if (_rtc.lostPower()) return false;
  DateTime n = _rtc.now();
  return (n.year() >= 2020 && n.year() <= 2099);
}

void RTCClockDS3231::nowLocal(String& tanggal, String& waktu){
  DateTime n = _rtc.now(); // diasumsikan sudah diset ke WIB
  char buf1[11], buf2[9];
  snprintf(buf1, sizeof(buf1), "%04d-%02d-%02d", n.year(), n.month(), n.day());
  snprintf(buf2, sizeof(buf2), "%02d:%02d:%02d", n.hour(), n.minute(), n.second());
  tanggal = buf1; waktu = buf2;
}

bool RTCClockDS3231::syncFromNTPAndSetRTC(uint32_t timeoutMs){
  // set NTP timezone WIB (UTC+7)
  configTime(7*3600, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  uint32_t t0 = millis();
  while ((millis() - t0) < timeoutMs){
    if (getLocalTime(&timeinfo, 1000)){
      DateTime dt(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      _rtc.adjust(dt);
      return true;
    }
  }
  return false;
}
