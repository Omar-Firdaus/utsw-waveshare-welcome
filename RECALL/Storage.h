#pragma once
#include <Arduino.h>
#include <FS.h>

fs::FS &storage_fs();
// "microSD", or "internal flash (bench)" in a RECALL_BENCH build.
const char *storage_medium();
uint64_t storage_min_free();

bool storage_mount(String &error);
// Touches the card itself, so a removed card is reported even after mounting.
bool storage_ready(String &error);
uint64_t storage_free_bytes();
bool storage_exists(const String &path);
bool storage_wipe(String &error);  // bench builds only

uint8_t storage_unit_number();
void storage_set_unit_number(uint8_t unit);
String storage_unit_id();
String storage_next_session_id();
bool storage_reserve_session(String &session_id, String &error);

bool storage_write_text(const String &path, const String &text, String &error);
