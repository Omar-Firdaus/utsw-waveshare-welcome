#include "Audio.h"
#include "Config.h"
#include "es7210.h"
#include "Storage.h"
#include <Wire.h>
#include <atomic>
#include <math.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace {

constexpr uint8_t ES7210_ADDR = 0x40;
constexpr uint8_t SPEAKER_AMP_PIN = 15;
constexpr uint32_t CHUNK = cfg::SAMPLE_RATE / 100;                              // 10 ms DMA frame
constexpr uint32_t FRAME = cfg::SAMPLE_RATE * cfg::VAD_FRAME_MS / 1000;        // 20 ms analysis frame
constexpr uint32_t CONFIRM_FRAMES = cfg::VAD_CONFIRM_MS / cfg::VAD_FRAME_MS;
constexpr uint32_t HANGOVER = cfg::ms(cfg::VAD_HANGOVER_MS);
constexpr uint32_t RING = cfg::SAMPLE_RATE * 8;                                 // 8 s in PSRAM
constexpr uint32_t WRITE_MAX = 8192;
constexpr uint32_t FLUSH_BYTES = cfg::ms(cfg::FLUSH_INTERVAL_MS) * 2;
constexpr uint8_t DUMP_REGS[] = {0x02, 0x07, 0x11, 0x13, 0x14, 0x16, 0x1B, 0x1C,
                                 0x20, 0x21, 0x22, 0x23, 0x40, 0x41, 0x43, 0x44};

enum Phase : int { IDLE, ARMED, RECORDING, DRAINING, DONE };

i2s_chan_handle_t rx = nullptr;
int lane = 0;
float lane_dbfs[2] = {-120, -120};
int reg_values[sizeof(DUMP_REGS)];

int16_t *ring = nullptr;
std::atomic<uint32_t> head{0}, tail{0};
std::atomic<int> phase{IDLE};
std::atomic<uint8_t> stop_request{uint8_t(EndReason::None)};
std::atomic<bool> write_failed{false};
std::atomic<bool> recording_flag{false};

std::atomic<uint32_t> st_samples{0};
std::atomic<int32_t> st_first{-1}, st_last{-1}, st_prompt{-1};
std::atomic<int32_t> level_cdb{-12000};
std::atomic<int64_t> start_us{0};

volatile uint32_t dma_overflows = 0;
std::atomic<uint32_t> overrun_events{0}, dropped_total{0}, read_errors{0};

struct Capture {
  RecordingRules rules;
  float threshold_var;
  uint32_t count, written, dropped;
  int32_t first_speech, last_speech_end, prompt_at;
  uint32_t run;
  int64_t frame_sum;
  uint64_t frame_sumsq;
  uint32_t frame_fill;
  double energy_total;
  uint32_t frames_total;
  uint16_t peak;
  uint32_t clipped;
  EndReason ended_by;
} cap;

File wav;
String wav_path;
String write_error;
uint32_t file_bytes = 0;
AudioResult result;

void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

void wav_header(uint8_t *h, uint32_t data_bytes) {
  memcpy(h, "RIFF", 4);
  put32(h + 4, 36 + data_bytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  put32(h + 16, 16);
  put16(h + 20, 1);                        // PCM
  put16(h + 22, 1);                        // mono
  put32(h + 24, cfg::SAMPLE_RATE);
  put32(h + 28, cfg::SAMPLE_RATE * 2);
  put16(h + 32, 2);
  put16(h + 34, 16);
  memcpy(h + 36, "data", 4);
  put32(h + 40, data_bytes);
}

bool update_header(uint32_t data_bytes) {
  uint8_t riff[4], data[4];
  put32(riff, 36 + data_bytes);
  put32(data, data_bytes);
  size_t end = wav.position();
  return wav.seek(4) && wav.write(riff, 4) == 4 && wav.seek(40) && wav.write(data, 4) == 4 && wav.seek(end);
}

bool codec_write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(ES7210_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

int codec_read(uint8_t reg) {
  Wire.beginTransmission(ES7210_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(ES7210_ADDR, uint8_t(1)) != 1) return -1;
  return Wire.read();
}

es7210_mic_gain_t pga_gain(int db) {
  switch (db) {
    case 0: return ES7210_MIC_GAIN_0DB;
    case 6: return ES7210_MIC_GAIN_6DB;
    case 12: return ES7210_MIC_GAIN_12DB;
    case 18: return ES7210_MIC_GAIN_18DB;
    case 24: return ES7210_MIC_GAIN_24DB;
    case 27: return ES7210_MIC_GAIN_27DB;
    case 33: return ES7210_MIC_GAIN_33DB;
    case 36: return ES7210_MIC_GAIN_36DB;
    default: return ES7210_MIC_GAIN_30DB;
  }
}

bool init_codec(String &error) {
  pinMode(SPEAKER_AMP_PIN, OUTPUT);
  digitalWrite(SPEAKER_AMP_PIN, LOW);
  Wire.beginTransmission(ES7210_ADDR);
  if (Wire.endTransmission() != 0) {
    error = "Microphone codec not found";
    return false;
  }
  es7210_dev_handle_t adc = nullptr;
  es7210_i2c_config_t i2c = {};
  i2c.i2c_addr = ES7210_ADDR;
  es7210_codec_config_t config = {};
  config.i2s_format = ES7210_I2S_FMT_I2S;
  config.mclk_ratio = 256;
  config.sample_rate_hz = cfg::SAMPLE_RATE;
  config.bit_width = ES7210_I2S_BITS_16B;
  config.mic_bias = ES7210_MIC_BIAS_2V87;
  config.mic_gain = pga_gain(cfg::MIC_PGA_GAIN_DB);
  config.flags.tdm_enable = false;
  if (es7210_new_codec(&i2c, &adc) != ESP_OK || es7210_config_codec(adc, &config) != ESP_OK ||
      es7210_config_volume(adc, cfg::MIC_DIGITAL_GAIN_DB) != ESP_OK ||
      !codec_write(0x16, 0x00)) {  // ALC off (reset default, written explicitly)
    error = "Microphone codec setup failed";
    return false;
  }
  for (size_t i = 0; i < sizeof(DUMP_REGS); ++i) reg_values[i] = codec_read(DUMP_REGS[i]);
  return true;
}

IRAM_ATTR bool on_dma_overflow(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  dma_overflows = dma_overflows + 1;
  return false;
}

bool init_i2s(String &error) {
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = 16;  // 160 ms, covers flash-write stalls
  channel.dma_frame_num = CHUNK;
  i2s_std_config_t config = {};
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg::SAMPLE_RATE);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk = GPIO_NUM_2;
  config.gpio_cfg.bclk = GPIO_NUM_48;
  config.gpio_cfg.ws = GPIO_NUM_38;
  config.gpio_cfg.dout = I2S_GPIO_UNUSED;
  config.gpio_cfg.din = GPIO_NUM_39;
  i2s_event_callbacks_t callbacks = {};
  callbacks.on_recv_q_ovf = on_dma_overflow;
  if (i2s_new_channel(&channel, nullptr, &rx) != ESP_OK ||
      i2s_channel_init_std_mode(rx, &config) != ESP_OK ||
      i2s_channel_register_event_callback(rx, &callbacks, nullptr) != ESP_OK ||
      i2s_channel_enable(rx) != ESP_OK) {
    error = "Audio input (I2S) setup failed";
    return false;
  }
  return true;
}

void choose_lane() {
  static int16_t buffer[CHUNK * 2];
  double sum[2] = {0, 0}, sumsq[2] = {0, 0};
  uint32_t n = 0;
  for (int i = 0; i < 80; ++i) {  // 300 ms settle, then 500 ms measured
    size_t bytes = 0;
    if (i2s_channel_read(rx, buffer, sizeof(buffer), &bytes, 1000) != ESP_OK) continue;
    if (i < 30) continue;
    for (size_t f = 0; f < bytes / 4; ++f, ++n) {
      for (int l = 0; l < 2; ++l) {
        sum[l] += buffer[2 * f + l];
        sumsq[l] += double(buffer[2 * f + l]) * buffer[2 * f + l];
      }
    }
  }
  float rms[2] = {0, 0};
  for (int l = 0; l < 2 && n; ++l) {
    double mean = sum[l] / n;
    rms[l] = sqrt(fmax(0.0, sumsq[l] / n - mean * mean));
    lane_dbfs[l] = rms_to_dbfs(rms[l]);
  }
  if (cfg::MIC_LANE >= 0) lane = cfg::MIC_LANE;
  else lane = rms[1] > 2 * rms[0] ? 1 : 0;
  Serial.printf("Mic lanes: L %.1f dBFS, R %.1f dBFS; using %s\n", lane_dbfs[0], lane_dbfs[1], lane ? "right" : "left");
}

void publish() {
  st_samples.store(cap.count);
  st_first.store(cap.first_speech);
  st_last.store(cap.last_speech_end);
  st_prompt.store(cap.prompt_at);
}

void finish(EndReason reason) {
  cap.ended_by = reason;
  publish();
  recording_flag = false;
  phase.store(DRAINING);
}

uint32_t current_limit(EndReason &why) {
  const RecordingRules &r = cap.rules;
  uint32_t limit = UINT32_MAX;
  why = EndReason::None;
  if (r.limit) { limit = r.limit; why = r.limit_reason; }
  if (r.limit_after_speech && cap.first_speech >= 0 && uint32_t(cap.first_speech) + r.limit_after_speech < limit) {
    limit = cap.first_speech + r.limit_after_speech;
    why = EndReason::TimeLimit;
  }
  if (r.limit_after_prompt && cap.prompt_at >= 0 && uint32_t(cap.prompt_at) + r.limit_after_prompt < limit) {
    limit = cap.prompt_at + r.limit_after_prompt;
    why = EndReason::PromptWindow;
  }
  return limit;
}

void push(const int16_t *x, uint32_t n) {
  uint32_t h = head.load(std::memory_order_relaxed);
  if (h - tail.load(std::memory_order_acquire) + n > RING) {
    overrun_events++;
    dropped_total += n;
    cap.dropped += n;
    return;
  }
  uint32_t index = h % RING, first = min(n, RING - index);
  memcpy(ring + index, x, first * 2);
  memcpy(ring, x + first, (n - first) * 2);
  head.store(h + n, std::memory_order_release);
  cap.written += n;
}

// Speech detection drives timing only; it never alters the saved audio.
EndReason end_frame() {
  float mean = float(cap.frame_sum) / FRAME;
  float var = fmaxf(0, float(cap.frame_sumsq) / FRAME - mean * mean);
  cap.frame_sum = 0;
  cap.frame_sumsq = 0;
  cap.frame_fill = 0;
  cap.energy_total += var;
  cap.frames_total++;

  bool above = var > cap.threshold_var;
  cap.run = above ? cap.run + 1 : 0;
  int64_t now = cap.count;
  bool continuing = cap.last_speech_end >= 0 && now - FRAME - cap.last_speech_end <= HANGOVER;
  if (above && (cap.run >= CONFIRM_FRAMES || continuing)) {
    if (cap.first_speech < 0) cap.first_speech = now - int64_t(cap.run) * FRAME;
    cap.last_speech_end = now;
  }

  const RecordingRules &r = cap.rules;
  int64_t silence = now - max<int32_t>(cap.last_speech_end, 0);
  if (r.prompt_silence && cap.prompt_at < 0 && silence >= r.prompt_silence) cap.prompt_at = now;
  if (r.end_silence_after_prompt && (cap.prompt_at >= 0 || r.prompt_already_used)) {
    int64_t since = now - max(max(cap.prompt_at, cap.last_speech_end), int32_t(0));
    if (since >= r.end_silence_after_prompt) return EndReason::SilenceAfterPrompt;
  }
  if (r.end_voice_stopped && cap.last_speech_end >= 0 && now - cap.last_speech_end >= r.end_voice_stopped)
    return EndReason::VoiceStopped;
  return EndReason::None;
}

void process(const int16_t *x, uint32_t n) {
  if (write_failed) { finish(EndReason::WriteError); return; }
  EndReason requested = EndReason(stop_request.load());
  if (requested != EndReason::None) { finish(requested); return; }
  EndReason why;
  uint32_t i = 0;
  while (i < n) {
    uint32_t limit = current_limit(why);
    if (cap.count >= limit) { finish(why); return; }
    uint32_t take = min(min(n - i, FRAME - cap.frame_fill), limit - cap.count);
    push(x + i, take);
    for (uint32_t k = 0; k < take; ++k) {
      int32_t v = x[i + k];
      cap.frame_sum += v;
      cap.frame_sumsq += uint32_t(v * v);
      uint16_t magnitude = v < 0 ? -v : v;
      if (magnitude > cap.peak) cap.peak = magnitude;
      if (v >= 32767 || v <= -32768) cap.clipped++;
    }
    cap.count += take;
    cap.frame_fill += take;
    i += take;
    if (cap.frame_fill == FRAME) {
      EndReason ended = end_frame();
      if (ended != EndReason::None) { finish(ended); return; }
    }
  }
  publish();
  if (cap.count >= current_limit(why)) finish(why);
}

void capture_task(void *) {
  static int16_t buffer[CHUNK * 2];
  static int16_t mono[CHUNK];
  for (;;) {
    size_t bytes = 0;
    if (i2s_channel_read(rx, buffer, sizeof(buffer), &bytes, 1000) != ESP_OK || bytes == 0) {
      read_errors++;
      continue;
    }
    uint32_t frames = bytes / 4;
    int64_t sum = 0;
    uint64_t sumsq = 0;
    for (uint32_t i = 0; i < frames; ++i) {
      int32_t v = buffer[2 * i + lane];
      mono[i] = v;
      sum += v;
      sumsq += uint32_t(v * v);
    }
    float mean = float(sum) / frames;
    level_cdb.store(int32_t(rms_to_dbfs(sqrtf(fmaxf(0, float(sumsq) / frames - mean * mean))) * 100));

    int p = phase.load();
    if (p == ARMED) {
      start_us.store(esp_timer_get_time());
      recording_flag = true;
      phase.store(RECORDING);
      p = RECORDING;
    }
    if (p == RECORDING) process(mono, frames);
  }
}

void finalize() {
  bool ok = !write_failed;
  String error = write_error;
  if (ok && !update_header(file_bytes)) { ok = false; error = "Could not update the WAV header"; }
  wav.flush();
  wav.close();
  uint32_t size = 0;
  if (ok) {
    File check = storage_fs().open(wav_path, FILE_READ);
    uint8_t header[44] = {};
    if (!check) {
      ok = false;
      error = "Saved file could not be reopened";
    } else {
      size = check.size();
      bool header_read = check.read(header, 44) == 44;
      check.close();
      if (!header_read || size != 44 + file_bytes || get32(header + 40) != file_bytes ||
          file_bytes != cap.written * 2) {
        ok = false;
        error = "Saved file size does not match the samples captured";
      }
    }
  }
  result.ok = ok;
  result.error = error;
  result.ended_by = ok ? cap.ended_by : EndReason::WriteError;
  result.samples = file_bytes / 2;
  result.bytes = size;
  result.first_speech = cap.first_speech;
  result.last_speech_end = cap.last_speech_end;
  result.prompt_at = cap.prompt_at;
  result.mean_rms = cap.frames_total ? sqrt(cap.energy_total / cap.frames_total) : 0;
  result.peak = cap.peak;
  result.clipped = cap.clipped;
  result.dropped = cap.dropped;
  phase.store(DONE);
}

void writer_task(void *) {
  uint32_t since_flush = 0;
  for (;;) {
    int p = phase.load();
    if (p != RECORDING && p != DRAINING) {
      since_flush = 0;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    uint32_t t = tail.load(std::memory_order_relaxed);
    uint32_t available = head.load(std::memory_order_acquire) - t;
    if (available >= WRITE_MAX || (p == DRAINING && available > 0)) {
      uint32_t index = t % RING;
      uint32_t n = min(min(available, RING - index), WRITE_MAX);
      if (!write_failed) {
        if (wav.write(reinterpret_cast<uint8_t *>(ring + index), n * 2) != n * 2) {
          write_error = "Writing to the SD card failed";
          write_failed = true;
        } else {
          file_bytes += n * 2;
          since_flush += n * 2;
        }
      }
      tail.store(t + n, std::memory_order_release);
      if (!write_failed && since_flush >= FLUSH_BYTES) {
        if (!update_header(file_bytes)) {
          write_error = "Writing to the SD card failed";
          write_failed = true;
        }
        wav.flush();
        since_flush = 0;
      }
      continue;
    }
    if (p == DRAINING) {
      finalize();
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

const char *end_reason_name(EndReason reason) {
  switch (reason) {
    case EndReason::TimeLimit: return "time_limit";
    case EndReason::Done: return "done";
    case EndReason::Stop: return "stop";
    case EndReason::Restart: return "restart";
    case EndReason::VoiceStopped: return "voice_stopped";
    case EndReason::SilenceAfterPrompt: return "silence_after_prompt";
    case EndReason::PromptWindow: return "prompt_window";
    case EndReason::SessionCap: return "session_cap";
    case EndReason::MaxLength: return "max_length";
    case EndReason::WriteError: return "write_error";
    default: return "none";
  }
}

float rms_to_dbfs(float rms) { return rms > 0 ? 20 * log10f(rms / 32768.0f) : -120; }
float dbfs_to_rms(float dbfs) { return 32768.0f * powf(10, dbfs / 20); }

bool audio_begin(String &error) {
  ring = static_cast<int16_t *>(heap_caps_malloc(RING * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ring) {
    error = "Not enough memory for the audio buffer";
    return false;
  }
  if (!init_codec(error) || !init_i2s(error)) return false;
  choose_lane();
  if (xTaskCreatePinnedToCore(capture_task, "capture", 4096, nullptr, configMAX_PRIORITIES - 3, nullptr, 0) != pdPASS ||
      xTaskCreatePinnedToCore(writer_task, "wav_writer", 6144, nullptr, 5, nullptr, 0) != pdPASS) {
    error = "Could not start the audio tasks";
    return false;
  }
  return true;
}

bool audio_start(const char *path, const RecordingRules &rules, float threshold_rms, String &error) {
  if (phase.load() != IDLE) {
    error = "Recorder is busy";
    return false;
  }
  wav_path = path;
  wav = storage_fs().open(path, FILE_WRITE);
  uint8_t header[44];
  wav_header(header, 0);
  if (!wav || wav.write(header, 44) != 44) {
    if (wav) wav.close();
    error = "Could not create the recording file on the SD card";
    return false;
  }
  wav.flush();

  cap = Capture{};
  cap.rules = rules;
  cap.threshold_var = threshold_rms * threshold_rms;
  cap.first_speech = cap.last_speech_end = cap.prompt_at = -1;
  cap.ended_by = EndReason::None;
  head = 0;
  tail = 0;
  file_bytes = 0;
  write_error = "";
  write_failed = false;
  stop_request = uint8_t(EndReason::None);
  publish();
  phase.store(ARMED);
  return true;
}

void audio_request_stop(EndReason reason) {
  uint8_t expected = uint8_t(EndReason::None);
  stop_request.compare_exchange_strong(expected, uint8_t(reason));
}

AudioStatus audio_status() {
  AudioStatus s;
  s.recording = recording_flag.load();
  s.samples = st_samples.load();
  s.first_speech = st_first.load();
  s.last_speech_end = st_last.load();
  s.prompt_at = st_prompt.load();
  return s;
}

bool audio_busy() { return phase.load() != IDLE; }

bool audio_take_result(AudioResult &out) {
  if (phase.load() != DONE) return false;
  out = result;
  phase.store(IDLE);
  return true;
}

int64_t audio_recording_start_us() { return start_us.load(); }
float audio_level_dbfs() { return level_cdb.load() / 100.0f; }
int audio_mic_lane() { return lane; }
uint32_t audio_overrun_events() { return overrun_events.load(); }
uint32_t audio_dropped_samples() { return dropped_total.load(); }
uint32_t audio_dma_overflows() { return dma_overflows; }
uint32_t audio_read_errors() { return read_errors.load(); }

String audio_config_json() {
  String s = "{\"sample_rate_hz\":" + String(cfg::SAMPLE_RATE) +
             ",\"bits_per_sample\":16,\"channels\":1,\"format\":\"WAV PCM, uncompressed\"" +
             ",\"codec\":\"ES7210\",\"mic_channel\":\"" + String(lane ? "right" : "left") + "\"" +
             ",\"channel_level_at_boot_dbfs\":[" + String(lane_dbfs[0], 1) + "," + String(lane_dbfs[1], 1) + "]" +
             ",\"analog_gain_db\":" + String(cfg::MIC_PGA_GAIN_DB) +
             ",\"digital_gain_db\":" + String(cfg::MIC_DIGITAL_GAIN_DB) +
             ",\"automatic_gain_control\":false,\"noise_suppression\":false" +
             ",\"codec_dc_high_pass\":\"ES7210 driver default\",\"codec_registers\":{";
  for (size_t i = 0; i < sizeof(DUMP_REGS); ++i) {
    char entry[24];
    snprintf(entry, sizeof(entry), "%s\"0x%02X\":%d", i ? "," : "", DUMP_REGS[i], reg_values[i]);
    s += entry;
  }
  return s + "}}";
}
