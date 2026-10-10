#include "Rtc.h"
#include <Wire.h>

namespace {

constexpr uint8_t PCF85063 = 0x51;
constexpr uint8_t REG_CONTROL_1 = 0x00, REG_SECONDS = 0x04;

uint8_t to_bcd(int v) { return uint8_t((v / 10) << 4 | (v % 10)); }
int from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return int64_t(era) * 146097 + int64_t(doe) - 719468;
}

void civil_from_days(int64_t z, int &y, unsigned &m, unsigned &d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = unsigned(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = int(yoe) + int(era) * 400 + (m <= 2);
}

}  // namespace

bool rtc_read(DateTime &time, bool &valid) {
  Wire.beginTransmission(PCF85063);
  Wire.write(REG_SECONDS);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(PCF85063, uint8_t(7)) != 7) return false;
  uint8_t r[7];
  for (auto &b : r) b = Wire.read();
  valid = !(r[0] & 0x80);
  time.second = from_bcd(r[0] & 0x7F);
  time.minute = from_bcd(r[1] & 0x7F);
  time.hour = from_bcd(r[2] & 0x3F);
  time.day = from_bcd(r[3] & 0x3F);
  time.month = from_bcd(r[5] & 0x1F);
  time.year = 2000 + from_bcd(r[6]);
  if (time.month < 1 || time.month > 12 || time.day < 1 || time.day > 31) valid = false;
  return true;
}

bool rtc_set(const DateTime &t) {
  int weekday = int((days_from_civil(t.year, t.month, t.day) + 4) % 7);  // 1970-01-01 was a Thursday
  uint8_t r[7] = {to_bcd(t.second), to_bcd(t.minute), to_bcd(t.hour), to_bcd(t.day),
                  uint8_t(weekday), to_bcd(t.month), to_bcd(t.year - 2000)};
  Wire.beginTransmission(PCF85063);
  Wire.write(REG_CONTROL_1);
  Wire.write(0x00);  // running, 24 h mode
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(PCF85063);
  Wire.write(REG_SECONDS);
  Wire.write(r, sizeof(r));
  return Wire.endTransmission() == 0;
}

bool rtc_parse(const String &text, DateTime &t) {
  int y, mo, d, h, mi, s;
  if (sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return false;
  if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) return false;
  t = {uint16_t(y), uint8_t(mo), uint8_t(d), uint8_t(h), uint8_t(mi), uint8_t(s)};
  return true;
}

String rtc_iso(const DateTime &t) {
  char text[24];
  snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u", t.year, t.month, t.day, t.hour, t.minute, t.second);
  return text;
}

DateTime rtc_add_ms(const DateTime &t, uint32_t ms) {
  int64_t seconds = days_from_civil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second + ms / 1000;
  int64_t days = seconds / 86400;
  int64_t rest = seconds % 86400;
  int y;
  unsigned m, d;
  civil_from_days(days, y, m, d);
  return {uint16_t(y), uint8_t(m), uint8_t(d), uint8_t(rest / 3600), uint8_t(rest / 60 % 60), uint8_t(rest % 60)};
}
