#include "BarkAudio.h"
#include "BarkClip.h"
#include "es8311.h"
#include "es7210.h"
#include <Wire.h>
#include <Arduino.h>
#include <atomic>
#include <driver/i2s_std.h>
namespace {
constexpr size_t RATE=16000, FRAMES=256;
i2s_chan_handle_t mic=nullptr, speaker=nullptr;
int16_t samples[FRAMES*2];
std::atomic<BarkState> state{BarkState::Starting};
QueueHandle_t requests=nullptr;
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


bool play_bark() {
 if (!start_output()) return false;
 for (size_t pos=0; pos<BARK_SAMPLE_COUNT; pos+=FRAMES) {
   size_t n=min(FRAMES,BARK_SAMPLE_COUNT-pos);
   for (size_t i=0;i<n;++i) samples[2*i]=samples[2*i+1]=bark_pcm[pos+i];
   if (!output(samples,n)) return false;
 }
 return finish_output();
}
void audio_task(void*) {
 if (!init_audio_bus()) { state=BarkState::Error; vTaskDelete(nullptr); return; }
 state=BarkState::Ready;
 uint8_t request;
 for (;;) {
   if (xQueueReceive(requests,&request,portMAX_DELAY)==pdTRUE) {
     state=BarkState::Playing;
     Serial.println("Woof!");
     if (!play_bark()) { digitalWrite(15,LOW); state=BarkState::Error; break; }
     state=BarkState::Ready;
   }
 }
 vTaskDelete(nullptr);
}
}
void bark_begin() {
 if (!init_codecs()) { state=BarkState::Error; return; }
 requests=xQueueCreate(8,sizeof(uint8_t));
 if (!requests || xTaskCreatePinnedToCore(audio_task,"doge_audio",6144,nullptr,3,nullptr,0)!=pdPASS) state=BarkState::Error;
}
bool bark_request() {
 if (!requests || state==BarkState::Starting || state==BarkState::Error) return false;
 uint8_t value=1;
 return xQueueSend(requests,&value,0)==pdTRUE;
}
BarkState bark_state() { return state.load(); }
