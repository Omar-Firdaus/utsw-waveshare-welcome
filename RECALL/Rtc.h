#pragma once
#include <Arduino.h>

struct DateTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
};

// PCF85063 real-time clock. `valid` is false if the oscillator stopped since it was set.
bool rtc_read(DateTime &time, bool &valid);
bool rtc_set(const DateTime &time);
bool rtc_parse(const String &text, DateTime &time);
String rtc_iso(const DateTime &time);
DateTime rtc_add_ms(const DateTime &time, uint32_t ms);
