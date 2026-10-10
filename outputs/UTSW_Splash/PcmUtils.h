#pragma once
#include <stddef.h>
#include <stdint.h>

inline int pcm_active_lane(const int16_t *stereo, size_t frames) {
  uint64_t energy[2] = {0, 0};
  for (size_t i = 0; i < frames; ++i) {
    for (int lane = 0; lane < 2; ++lane) {
      int sample = stereo[2*i+lane];
      energy[lane] += sample < 0 ? -sample : sample;
    }
  }
  return energy[1] > energy[0] ? 1 : 0;
}

inline int16_t pcm_playback_sample(int16_t sample, size_t index, size_t count) {
  if (count == 0 || index >= count) return 0;
  int value = int(sample) * 3;
  if (value > 32767) value = 32767;
  if (value < -32767) value = -32767;
  size_t edge = index < count-1-index ? index : count-1-index;
  if (edge < 80) value = value * int(edge) / 80;
  return (int16_t)value;
}

// MSM261S4030H0 sends 24 significant bits in a 32-bit I2S word.
// Waveshare's reference uses a 14-bit shift to boost the microphone signal.
inline int16_t pcm_from_mic32(int32_t raw) {
  int32_t value = raw / 16384;
  if (value > 32767) value = 32767;
  if (value < -32768) value = -32768;
  return (int16_t)value;
}
