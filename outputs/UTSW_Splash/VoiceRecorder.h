#pragma once
#include <stdint.h>
enum class RecorderState { Starting, Ready, Chime, Welcome, Recording, Full, Playing, Error };
void recorder_begin();
void recorder_chime();
void recorder_hold(bool held);
RecorderState recorder_state();
uint32_t recorder_milliseconds();

uint32_t recorder_level();

// Plays the embedded introduction once; returns false while audio is busy.
bool recorder_welcome();
uint32_t recorder_welcome_finished();
uint32_t recorder_voice_level();
