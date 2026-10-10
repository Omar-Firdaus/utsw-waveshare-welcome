#include "Storage.h"
#include "Config.h"
#include <Preferences.h>
#include <SD_MMC.h>
#include "diskio_impl.h"
#include "diskio_sdmmc.h"

namespace {

constexpr int SD_CLK = 14, SD_CMD = 17, SD_D0 = 16;
constexpr const char *ROOT = "/RECALL";
bool mounted = false;
Preferences prefs;

String session_id_for(uint32_t counter) {
  char id[24];
  snprintf(id, sizeof(id), "%s-%04lu", storage_unit_id().c_str(), (unsigned long)counter);
  return id;
}

uint32_t first_free_counter() {
  prefs.begin("recall", true);
  uint32_t counter = prefs.getUInt("next", 1);
  prefs.end();
  while (mounted && SD_MMC.exists(String(ROOT) + "/" + session_id_for(counter))) ++counter;
  return counter;
}

}  // namespace

bool storage_mount(String &error) {
  if (mounted) SD_MMC.end();
  mounted = false;
  BYTE pdrv = 0xFF;
  ff_diskio_get_drive(&pdrv);
  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0) || !SD_MMC.begin("/sdcard", true, false)) {
    error = "No SD card found";
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    error = "No SD card found";
    return false;
  }
  if (pdrv != 0xFF) ff_sdmmc_set_disk_status_check(pdrv, true);
  mounted = true;
  if (!SD_MMC.exists(ROOT) && !SD_MMC.mkdir(ROOT)) {
    error = "Could not create /RECALL on the SD card";
    return false;
  }
  return true;
}

bool storage_ready(String &error) {
  if (!mounted && !storage_mount(error)) return false;
  if (!SD_MMC.exists(ROOT)) {
    error = "SD card missing or unreadable";
    return false;
  }
  return true;
}

uint64_t storage_free_bytes() {
  if (!mounted) return 0;
  return SD_MMC.totalBytes() - SD_MMC.usedBytes();
}

bool storage_exists(const String &path) { return mounted && SD_MMC.exists(path); }

uint8_t storage_unit_number() {
  prefs.begin("recall", true);
  uint8_t unit = prefs.getUChar("unit", cfg::DEFAULT_UNIT);
  prefs.end();
  return unit;
}

void storage_set_unit_number(uint8_t unit) {
  prefs.begin("recall", false);
  prefs.putUChar("unit", unit);
  prefs.end();
}

String storage_unit_id() {
  char id[16];
  snprintf(id, sizeof(id), "RECALL-%02u", storage_unit_number());
  return id;
}

String storage_next_session_id() { return session_id_for(first_free_counter()); }

bool storage_reserve_session(String &session_id, String &error) {
  if (!storage_ready(error)) return false;
  uint32_t counter = first_free_counter();
  // Persist before creating the folder so an ID is never handed out twice.
  prefs.begin("recall", false);
  bool saved = prefs.putUInt("next", counter + 1) == sizeof(uint32_t);
  prefs.end();
  session_id = session_id_for(counter);
  if (!saved) {
    error = "Could not store the session counter";
    return false;
  }
  if (!SD_MMC.mkdir(String(ROOT) + "/" + session_id)) {
    error = "Could not create the session folder";
    return false;
  }
  return true;
}

bool storage_write_text(const String &path, const String &text, String &error) {
  String temp = path + ".tmp";
  File file = SD_MMC.open(temp, FILE_WRITE);
  if (!file) {
    error = "Could not write the session log";
    return false;
  }
  size_t written = file.print(text);
  file.flush();
  file.close();
  File check = SD_MMC.open(temp, FILE_READ);
  bool ok = written == text.length() && check && check.size() == text.length();
  if (check) check.close();
  if (!ok) {
    error = "Could not write the session log";
    return false;
  }
  // FAT rename does not replace; if power is lost between these two steps the .tmp is the log.
  if (SD_MMC.exists(path)) SD_MMC.remove(path);
  if (!SD_MMC.rename(temp, path)) {
    error = "Could not write the session log";
    return false;
  }
  return true;
}
