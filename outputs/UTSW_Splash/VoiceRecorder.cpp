#include "VoiceRecorder.h"
#include "PcmUtils.h"
#include "WelcomeAudio.h"
#include "es8311.h"
#include "es7210.h"
#include <Wire.h>
#include <Arduino.h>
#include <atomic>
#include <math.h>
#include <esp_heap_caps.h>
#include <driver/i2s_std.h>

namespace {
constexpr size_t RATE = 16000, CAPACITY = RATE * 30, FRAMES = 256;
std::atomic<RecorderState> state{RecorderState::Starting};
std::atomic<bool> held{false}, chime{false};
std::atomic<uint32_t> duration{0}, level{0};
std::atomic<bool> welcome_requested{false};
std::atomic<uint32_t> welcome_finished{0}, voice_level{0};
i2s_chan_handle_t mic = nullptr, speaker = nullptr;
int16_t *recording = nullptr;
int16_t samples[FRAMES * 2];
int16_t mic_samples[FRAMES * 2];
size_t count = 0;

// V2 uses ES8311 output and ES7210 input on a shared I2S bus.
// GPIO15 is amplifier enable, NOT a microphone clock.
bool codec_ok(esp_err_t result, const char *step) {
  Serial.printf("%s: %s\n", step, esp_err_to_name(result));
  return result == ESP_OK;
}

bool init_codecs() {
  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  Wire.beginTransmission(0x18);
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(0x40);
  if (Wire.endTransmission() != 0) return false;
  es8311_handle_t dac = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  if (!dac) return false;
  es8311_clock_config_t clock = {};
  clock.mclk_from_mclk_pin = true;
  clock.mclk_frequency = RATE * 256;
  clock.sample_frequency = RATE;
  if (!codec_ok(es8311_init(dac, &clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16), "ES8311 initialize") ||
      !codec_ok(es8311_voice_volume_set(dac, 65, nullptr), "ES8311 volume") ||
      !codec_ok(es8311_microphone_config(dac, false), "ES8311 analog mode")) return false;
  es7210_dev_handle_t adc = nullptr;
  es7210_i2c_config_t i2c = {};
  i2c.i2c_addr = 0x40;
  if (es7210_new_codec(&i2c, &adc) != ESP_OK) return false;
  es7210_codec_config_t config = {};
  config.i2s_format = ES7210_I2S_FMT_I2S;
  config.mclk_ratio = 256;
  config.sample_rate_hz = RATE;
  config.bit_width = ES7210_I2S_BITS_16B;
  config.mic_bias = ES7210_MIC_BIAS_2V87;
  config.mic_gain = ES7210_MIC_GAIN_36DB;
  config.flags.tdm_enable = false;
  if (!codec_ok(es7210_config_codec(adc, &config), "ES7210 initialize") ||
      !codec_ok(es7210_config_volume(adc, 0), "ES7210 digital gain")) return false;
  Serial.println("V2 ES8311 + ES7210 codecs configured");
  return true;
}

bool init_audio_bus() {
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = 4;
  channel.dma_frame_num = FRAMES;
  channel.auto_clear = true;
  if (i2s_new_channel(&channel, &speaker, &mic) != ESP_OK) return false;
  i2s_std_config_t config = {};
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk = GPIO_NUM_2;
  config.gpio_cfg.bclk = GPIO_NUM_48;
  config.gpio_cfg.ws = GPIO_NUM_38;
  config.gpio_cfg.dout = GPIO_NUM_47;
  config.gpio_cfg.din = GPIO_NUM_39;
  if (i2s_channel_init_std_mode(speaker, &config) != ESP_OK ||
      i2s_channel_init_std_mode(mic, &config) != ESP_OK) return false;
  // Keep the shared codec clocks running; the amplifier is enabled only
  // for output. RX is enabled only during a deliberate recording hold.
  return i2s_channel_enable(speaker) == ESP_OK;
}

bool output(const int16_t *pcm, size_t frames) {
  size_t sent = 0;
  return i2s_channel_write(speaker, pcm, frames * 4, &sent, 200) == ESP_OK && sent == frames * 4;
}

bool finish_output() {
  // Flush the DMA queue with silence so the last sample never loops.
  memset(samples, 0, sizeof(samples));
  for (int i = 0; i < 6; ++i) if (!output(samples, FRAMES)) return false;
  vTaskDelay(pdMS_TO_TICKS(80));
  digitalWrite(15, LOW);
  return true;
}

bool start_output() {
  memset(samples, 0, sizeof(samples));
  // Allow the DAC clock/analog output to settle before the sound starts.
  for (int i = 0; i < 8; ++i) if (!output(samples, FRAMES)) return false;
  digitalWrite(15, HIGH);
  return true;
}

bool play_chime() {
  Serial.println("Speaker test: playing two notes");
  if (!start_output()) return false;
  for (int note = 0; note < 2; ++note) {
    const size_t length = RATE * 18 / 100;
    for (size_t pos = 0; pos < length; pos += FRAMES) {
      size_t n = min(FRAMES, length - pos);
      for (size_t i = 0; i < n; ++i) {
        float t = float(pos + i) / length;
        float envelope = sinf(3.14159265f * t);
        int16_t value = 4500 * envelope * sinf(6.2831853f * (note ? 880 : 660) * (pos + i) / RATE);
        samples[2*i] = samples[2*i+1] = value;
      }
      if (!output(samples, n)) return false;
    }
  }
  return finish_output();
}

bool play_welcome() {
  Serial.printf("Welcome speech started: %u samples\n", (unsigned)WELCOME_SAMPLE_COUNT);
  if (!start_output()) return false;
  for (size_t pos = 0; pos < WELCOME_SAMPLE_COUNT; pos += FRAMES) {
    size_t n = min(FRAMES, WELCOME_SAMPLE_COUNT-pos);
    uint64_t energy = 0;
    for (size_t i = 0; i < n; ++i) {
      int16_t value = welcome_pcm[pos+i];
      samples[2*i] = samples[2*i+1] = value;
      energy += int32_t(value) * int32_t(value);
    }
    voice_level = min(uint32_t(32767), (uint32_t)(sqrtf(float(energy)/n)*5));
    if (!output(samples, n)) return false;
  }
  voice_level = 0;
  if (!finish_output()) return false;
  Serial.println("Welcome speech finished");
  return true;
}

bool play_recording() {
  if (!count) return true;
  Serial.printf("Playback: %u samples (%lu ms)\n", (unsigned)count, (unsigned long)duration.load());
  if (!start_output()) return false;
  for (size_t pos = 0; pos < count; pos += FRAMES) {
    size_t n = min(FRAMES, count - pos);
    for (size_t i = 0; i < n; ++i) {
      int16_t value = pcm_playback_sample(recording[pos+i], pos+i, count);
      samples[2*i] = samples[2*i+1] = value;
    }
    if (!output(samples, n)) return false;
  }
  return finish_output();
}

void fail() {
  if (mic) i2s_channel_disable(mic);
  if (speaker) i2s_channel_disable(speaker);
  digitalWrite(15, LOW);
  held = false;
  voice_level = 0;
  state = RecorderState::Error;
  Serial.println("Recorder initialization or I2S transfer failed");
}

void audio_task(void *) {
  recording = (int16_t *)heap_caps_malloc(CAPACITY * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!recording || !init_audio_bus()) {
    fail(); vTaskDelete(nullptr); return;
  }
  state = RecorderState::Ready;
  for (;;) {
    if (welcome_requested.exchange(false)) {
      if (!play_welcome()) { fail(); break; }
      state = RecorderState::Ready;
      ++welcome_finished;
    }
    if (chime.exchange(false) && state == RecorderState::Ready) {
      state = RecorderState::Chime;
      if (!play_chime()) { fail(); break; }
      state = RecorderState::Ready;
    }
    if (held && state == RecorderState::Ready) {
      count = 0; duration = 0; level = 0;
      if (i2s_channel_enable(mic) != ESP_OK) { fail(); break; }
      // Discard any queued samples left from the previous recording.
      size_t discarded;
      for (int i = 0; i < 8; ++i) {
        if (i2s_channel_read(mic, mic_samples, sizeof(mic_samples), &discarded, 0) != ESP_OK) break;
      }
      Serial.println("Recording started: V2 ES7210 stereo input");
      state = RecorderState::Recording;
    }
    if (state == RecorderState::Recording && held) {
      size_t bytes = 0;
      if (i2s_channel_read(mic, mic_samples, sizeof(mic_samples), &bytes, 100) != ESP_OK || bytes == 0) { fail(); break; }
      size_t frames = bytes / 4;
      int lane = pcm_active_lane(mic_samples, frames);
      uint32_t peak = 0;
      for (size_t i = 0; i < frames && count < CAPACITY; ++i) {
        int16_t value = mic_samples[2*i+lane];
        recording[count++] = value;
        peak = max(peak, (uint32_t)abs(int(value)));
      }
      level = peak;
      duration = count * 1000 / RATE;
      if (count == CAPACITY) {
        i2s_channel_disable(mic);
        state = RecorderState::Full;
      }
    }
    if (!held && (state == RecorderState::Recording || state == RecorderState::Full)) {
      if (state == RecorderState::Recording) i2s_channel_disable(mic);
      state = RecorderState::Playing;
      if (!play_recording()) { fail(); break; }
      // No repeat: wipe the captured clip after its single playback.
      memset(recording, 0, count * sizeof(int16_t));
      count = 0; duration = 0; level = 0;
      state = RecorderState::Ready;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  vTaskDelete(nullptr);
}
}
void recorder_begin() {
  // Called from setup before the LVGL loop starts, keeping codec I2C
  // initialization separate from touch-controller I2C transactions.
  if (!init_codecs()) {
    Serial.println("V2 codec initialization failed: check I2C addresses 0x18 and 0x40");
    state = RecorderState::Error;
    return;
  }
  if (xTaskCreatePinnedToCore(audio_task, "voice_audio", 6144, nullptr, 3, nullptr, 0) != pdPASS) state = RecorderState::Error;
}
void recorder_chime() { if (state == RecorderState::Ready) chime = true; }
void recorder_hold(bool pressed) {
  Serial.println(pressed ? "Touch pressed" : "Touch released");
  held = pressed;
}
RecorderState recorder_state() { return state.load(); }
uint32_t recorder_milliseconds() { return duration.load(); }

uint32_t recorder_level() { return level.load(); }

bool recorder_welcome() {
  RecorderState expected = RecorderState::Ready;
  if (!state.compare_exchange_strong(expected, RecorderState::Welcome)) return false;
  welcome_requested = true;
  return true;
}
uint32_t recorder_welcome_finished() { return welcome_finished.load(); }
uint32_t recorder_voice_level() { return voice_level.load(); }
