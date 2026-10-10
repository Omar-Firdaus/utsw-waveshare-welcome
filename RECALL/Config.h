#pragma once
#include <stdint.h>

// Bump on every build used with participants (addendum 8.6 version lock).
#define RECALL_FIRMWARE_VERSION "0.1.0"

// Bench build (scripts/build-recall.sh --bench): records to internal flash instead of the
// microSD card and adds serial test commands. Never use a bench build with participants.
#ifndef RECALL_BENCH
#define RECALL_BENCH 0
#endif

namespace cfg {

constexpr uint32_t SAMPLE_RATE = 44100;
constexpr uint32_t ms(uint32_t milliseconds) { return uint64_t(milliseconds) * SAMPLE_RATE / 1000; }

// Unit number used until one is stored with the serial command `unit N`.
constexpr uint8_t DEFAULT_UNIT = 1;
constexpr uint64_t MIN_FREE_BYTES = 40ull * 1024 * 1024;

// Fixed input chain: analog PGA gain, 0 dB digital gain, codec ALC off.
constexpr int MIC_PGA_GAIN_DB = 30;
constexpr int MIC_DIGITAL_GAIN_DB = 0;
// ES7210 lane carrying the microphone (0 = left, 1 = right, -1 = choose at boot).
constexpr int MIC_LANE = -1;

// Speech detection. Starting points for tuning, not protocol values.
constexpr uint32_t VAD_FRAME_MS = 20;
constexpr float VAD_MARGIN_DB = 10.0f;
constexpr uint32_t VAD_CONFIRM_MS = 200;
constexpr uint32_t VAD_HANGOVER_MS = 300;
constexpr float DEFAULT_NOISE_FLOOR_DBFS = -60.0f;  // only if task 0 was not recorded
constexpr float MIN_NOISE_FLOOR_DBFS = -80.0f;

// Protocol timings (addendum section 5). "Suggested" values are not in the addendum.
constexpr uint32_t T0_LENGTH_MS = 10000;
constexpr uint32_t PROMPT_SILENCE_MS = 15000;
constexpr uint32_t END_SILENCE_AFTER_PROMPT_MS = 15000;  // suggested
constexpr uint32_t T1_VOICE_STOP_MS = 1000;              // suggested
constexpr uint32_t T1_MIN_VALID_MS = 3000;
constexpr uint32_t T1_MAX_ATTEMPT_MS = 60000;            // safety cap, not in addendum
constexpr uint8_t T1_MAX_ATTEMPTS = 3;
constexpr uint8_t T1_VALID_NEEDED = 2;
constexpr uint32_t T3_AFTER_FIRST_SPEECH_MS = 75000;
constexpr uint32_t T3_AFTER_PROMPT_MS = 20000;
constexpr uint32_t T4_LENGTH_MS = 60000;
constexpr uint32_t T5_LENGTH_MS = 60000;
constexpr uint32_t SESSION_CAP_MS = 360000;
constexpr uint32_t T5_AUTO_SKIP_AFTER_MS = 300000;       // suggested

constexpr uint32_t FLUSH_INTERVAL_MS = 2000;
constexpr uint32_t END_MESSAGE_MS = 2500;

}  // namespace cfg
