#pragma once
#include <Arduino.h>

enum class EndReason : uint8_t {
  None,
  TimeLimit,           // fixed length reached (tasks 0, 4, 5; task 3 75 s after first speech)
  Done,                // Done pressed
  Stop,                // Stop chosen by the examiner
  Restart,             // Restart chosen by the examiner (file kept, marked interrupted)
  VoiceStopped,        // task 1: silence after the voice
  SilenceAfterPrompt,  // silence continued after the one prompt
  PromptWindow,        // task 3: 20 s after the prompt
  SessionCap,          // 6 minute session cap
  MaxLength,           // safety cap on a task 1 attempt
  WriteError,
};
const char *end_reason_name(EndReason reason);

// All lengths are in samples of the 44.1 kHz capture clock; 0 disables a rule.
struct RecordingRules {
  uint32_t limit = 0;                  // from Start
  EndReason limit_reason = EndReason::TimeLimit;
  uint32_t limit_after_speech = 0;     // from the onset of the first detected speech
  uint32_t limit_after_prompt = 0;     // from the prompt
  uint32_t prompt_silence = 0;         // silence before the prompt is raised
  uint32_t end_silence_after_prompt = 0;
  bool prompt_already_used = false;    // the task's one prompt was shown in an earlier attempt
  uint32_t end_voice_stopped = 0;      // silence after speech that ends the recording
};

struct AudioStatus {
  bool recording;
  uint32_t samples;
  int32_t first_speech;     // sample index of speech onset, -1 if none
  int32_t last_speech_end;  // -1 if none
  int32_t prompt_at;        // sample index the prompt condition was met, -1 if none
};

struct AudioResult {
  bool ok;
  String error;
  EndReason ended_by;
  uint32_t samples;          // samples written to the file
  uint32_t bytes;            // file size on the card
  int32_t first_speech;
  int32_t last_speech_end;
  int32_t prompt_at;
  float mean_rms;            // DC-removed RMS over the whole recording
  uint16_t peak;
  uint32_t clipped;
  uint32_t dropped;          // samples lost to a full ring buffer
};

bool audio_begin(String &error);
bool audio_start(const char *path, const RecordingRules &rules, float threshold_rms, String &error);
void audio_request_stop(EndReason reason);
AudioStatus audio_status();
bool audio_busy();
bool audio_take_result(AudioResult &result);
int64_t audio_recording_start_us();

float audio_level_dbfs();
int audio_mic_lane();
uint32_t audio_overrun_events();
uint32_t audio_dropped_samples();
uint32_t audio_dma_overflows();
uint32_t audio_read_errors();
String audio_config_json();

float rms_to_dbfs(float rms);
float dbfs_to_rms(float dbfs);
