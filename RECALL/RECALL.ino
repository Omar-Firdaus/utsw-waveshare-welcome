// Project RECALL speech capture firmware.
// Waveshare ESP32-S3-Touch-LCD-1.85C V2: ES7210 microphone codec, microSD (SDMMC 1-bit),
// PCF85063 real-time clock, 360x360 round touch display. See README.md for build settings.

#include "LVGL_Driver.h"
#include "Audio.h"
#include "Config.h"
#include "Rtc.h"
#include "Session.h"
#include "Storage.h"

static void print_status() {
  DateTime now{};
  bool valid = false;
  bool read = rtc_read(now, valid);
  String error;
  bool card = storage_ready(error);
  Serial.printf("Firmware %s (%s %s)\n", RECALL_FIRMWARE_VERSION, __DATE__, __TIME__);
  Serial.printf("Unit %s, next session %s\n", storage_unit_id().c_str(),
                card ? storage_next_session_id().c_str() : "-");
  Serial.printf("Clock %s%s\n", read ? rtc_iso(now).c_str() : "unreadable", valid ? "" : " (not set)");
  if (card) Serial.printf("SD card %.1f MB free\n", storage_free_bytes() / 1048576.0);
  else Serial.printf("SD card: %s\n", error.c_str());
  Serial.printf("Mic level %.1f dBFS, overruns %lu, DMA overflows %lu\n", audio_level_dbfs(),
                (unsigned long)audio_overrun_events(), (unsigned long)audio_dma_overflows());
}

static void run_command(String line) {
  line.trim();
  if (line.startsWith("time ")) {
    DateTime t;
    if (!rtc_parse(line.substring(5), t)) Serial.println("Usage: time YYYY-MM-DD HH:MM:SS");
    else Serial.println(rtc_set(t) ? "Clock set" : "Clock write failed");
  } else if (line.startsWith("unit ")) {
    int unit = line.substring(5).toInt();
    if (!session_idle()) Serial.println("Finish the current session first");
    else if (unit < 1 || unit > 99) Serial.println("Usage: unit N (1-99)");
    else {
      storage_set_unit_number(unit);
      Serial.println("Unit set to " + storage_unit_id());
    }
  } else if (line == "status") {
    print_status();
    return;
  } else {
    Serial.println("Commands: status | time YYYY-MM-DD HH:MM:SS | unit N");
    return;
  }
  session_refresh();
}

static void poll_serial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) run_command(line);
      line = "";
    } else if (line.length() < 64) {
      line += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("RECALL firmware %s\n", RECALL_FIRMWARE_VERSION);
  I2C_Init();
  TCA9554PWR_Init(0x00);
  Backlight_Init();
  Set_Backlight(0);
  LCD_Init();
  Lvgl_Init();

  String error;
  if (!audio_begin(error)) Serial.println("Audio: " + error);
  String sd_error;
  if (!storage_mount(sd_error)) Serial.println("SD: " + sd_error);
  session_begin(error);
  lv_refr_now(nullptr);
  Set_Backlight(80);
  print_status();
}

void loop() {
  Lvgl_Loop();
  session_loop();
  poll_serial();
  delay(5);
}
