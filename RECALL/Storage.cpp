#include "Storage.h"
#include "Config.h"
#include <Preferences.h>
#include <SD_MMC.h>
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#if RECALL_BENCH
#include <FFat.h>
#endif

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
  while (mounted && storage_fs().exists(String(ROOT) + "/" + session_id_for(counter))) ++counter;
  return counter;
}

#if RECALL_BENCH
bool mount_medium(String &error) {
  if (!FFat.begin(true)) {
    error = "Internal flash storage failed";
    return false;
  }
  return true;
}
#else
bool mount_medium(String &error) {
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
  return true;
}
#endif

}  // namespace

#if RECALL_BENCH
fs::FS &storage_fs() { return FFat; }
const char *storage_medium() { return "internal flash (bench)"; }
uint64_t storage_min_free() { return 2ull * 1024 * 1024; }
uint64_t storage_free_bytes() { return mounted ? FFat.freeBytes() : 0; }
bool storage_wipe(String &error) {
  FFat.end();
  mounted = false;
  if (!FFat.format()) {
    error = "Format failed";
    return false;
  }
  return storage_mount(error);
}
#else
fs::FS &storage_fs() { return SD_MMC; }
const char *storage_medium() { return "microSD"; }
uint64_t storage_min_free() { return cfg::MIN_FREE_BYTES; }
uint64_t storage_free_bytes() { return mounted ? SD_MMC.totalBytes() - SD_MMC.usedBytes() : 0; }
bool storage_wipe(String &error) {
  error = "Not available on the SD card build";
  return false;
}
#endif

bool storage_mount(String &error) {
  bool was_mounted = mounted;
  mounted = false;
  if (was_mounted && !RECALL_BENCH) SD_MMC.end();
  if (!mount_medium(error)) return false;
  mounted = true;
  if (!storage_fs().exists(ROOT) && !storage_fs().mkdir(ROOT)) {
    error = "Could not create /RECALL";
    return false;
  }
  return true;
}

bool storage_ready(String &error) {
  if (!mounted && !storage_mount(error)) return false;
  if (!storage_fs().exists(ROOT)) {
    error = "SD card missing or unreadable";
    return false;
  }
  return true;
}

bool storage_exists(const String &path) { return mounted && storage_fs().exists(path); }

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
  if (!storage_fs().mkdir(String(ROOT) + "/" + session_id)) {
    error = "Could not create the session folder";
    return false;
  }
  return true;
}

bool storage_write_text(const String &path, const String &text, String &error) {
  fs::FS &fs = storage_fs();
  String temp = path + ".tmp";
  File file = fs.open(temp, FILE_WRITE);
  if (!file) {
    error = "Could not write the session log";
    return false;
  }
  size_t written = file.print(text);
  file.flush();
  file.close();
  File check = fs.open(temp, FILE_READ);
  bool ok = written == text.length() && check && check.size() == text.length();
  if (check) check.close();
  if (!ok) {
    error = "Could not write the session log";
    return false;
  }
  // FAT rename does not replace; if power is lost between these two steps the .tmp is the log.
  if (fs.exists(path)) fs.remove(path);
  if (!fs.rename(temp, path)) {
    error = "Could not write the session log";
    return false;
  }
  return true;
}
