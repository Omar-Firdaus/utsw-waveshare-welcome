#pragma once
#include <Arduino.h>

void session_begin(const String &hardware_error);
void session_loop();
// True on the session start screen, before a session ID is reserved.
bool session_idle();
void session_refresh();
