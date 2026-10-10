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
  Serial.printf("Firmware %s (%s %s)%s\n", RECALL_FIRMWARE_VERSION, __DATE__, __TIME__,
                RECALL_BENCH ? " BENCH BUILD" : "");
  Serial.printf("Unit %s, next session %s\n", storage_unit_id().c_str(),
                card ? storage_next_session_id().c_str() : "-");
  Serial.printf("Clock %s%s\n", read ? rtc_iso(now).c_str() : "unreadable", valid ? "" : " (not set)");
  if (card) Serial.printf("Storage %s, %.1f MB free\n", storage_medium(), storage_free_bytes() / 1048576.0);
  else Serial.printf("Storage: %s\n", error.c_str());
  Serial.printf("Mic level %.1f dBFS, overruns %lu, DMA overflows %lu\n", audio_level_dbfs(),
                (unsigned long)audio_overrun_events(), (unsigned long)audio_dma_overflows());
}

#if RECALL_BENCH
#include "Ui.h"
#include <esp_rom_crc.h>
#include <mbedtls/base64.h>

static void list_dir(const String &path) {
  File dir = storage_fs().open(path);
  if (!dir || !dir.isDirectory()) {
    Serial.println("not a directory: " + path);
    return;
  }
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) list_dir(f.path());
    else Serial.printf("%10lu  %s\n", (unsigned long)f.size(), f.path());
  }
}

// Base64 dump framed by BEGIN/END lines; scripts/recall_serial.py decodes it.
static void send_file(const String &path) {
  File f = storage_fs().open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    Serial.println("ERROR no such file " + path);
    return;
  }
  Serial.printf("BEGIN %lu %s\n", (unsigned long)f.size(), path.c_str());
  static uint8_t raw[576];
  static unsigned char text[800];
  uint32_t crc = 0;
  for (size_t n; (n = f.read(raw, sizeof(raw))) > 0;) {
    crc = esp_rom_crc32_le(crc, raw, n);
    size_t len = 0;
    mbedtls_base64_encode(text, sizeof(text), &len, raw, n);
    Serial.write(text, len);
    Serial.write('\n');
  }
  f.close();
  Serial.printf("END %08lx\n", (unsigned long)crc);
}

static bool run_bench_command(const String &line) {
  String result;
  if (line.startsWith("press ")) {
    ui::press(line.substring(6), result);
    Serial.println(result);
  } else if (line == "screen") {
    Serial.print(ui::describe());
    Serial.println("--");
  } else if (line == "ls" || line.startsWith("ls ")) {
    list_dir(line.length() > 3 ? line.substring(3) : String("/RECALL"));
    Serial.printf("free %.1f MB\n", storage_free_bytes() / 1048576.0);
  } else if (line.startsWith("get ")) {
    send_file(line.substring(4));
  } else if (line == "wipe") {
    if (!session_idle()) Serial.println("Finish the current session first");
    else Serial.println(storage_wipe(result) ? "Flash storage wiped" : result);
    session_refresh();
  } else {
    return false;
  }
  return true;
}
#endif

static void run_command(String line) {
  line.trim();
#if RECALL_BENCH
  if (run_bench_command(line)) return;
#endif
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
    } else if (line.length() < 120) {
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
