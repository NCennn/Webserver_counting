#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

class RTCClockDS3231 {
public:
  bool begin(int sda=5, int scl=6);
  bool isValid(); // false jika lost power atau tanggal out of range
  void nowLocal(String& tanggal, String& waktu); // WIB (UTC+7)
  bool syncFromNTPAndSetRTC(uint32_t timeoutMs=7000); // opsional (butuh internet)
private:
  RTC_DS3231 _rtc;
};