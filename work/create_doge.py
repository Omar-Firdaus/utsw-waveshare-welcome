from pathlib import Path
import shutil, wave, struct
from PIL import Image
src=Path('outputs/UTSW_Splash'); dst=Path('outputs/Doge_Bark')
for p in src.iterdir():
 if p.name.startswith(('Display_', 'LVGL_', 'I2C_', 'TCA9554', 'Touch_', 'esp_lcd_', 'es8311', 'es7210', 'LICENSE-')): shutil.copy2(p,dst/p.name)
image=Path('/Users/omarfirdaus/.codex/generated_images/01a078e7-92ec-76e3-8fff-d2ae84499490/exec-8e052dee-55c0-4fce-833c-7a6cb06bd27f.png')
shutil.copy2(image,dst/'assets/doge.png')
im=Image.open(image).convert('RGB').resize((280,280), Image.Resampling.LANCZOS)
# Encode the original artwork at the display size, honoring LVGL's color byte order.
colors=[((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in im.getdata()]
with (dst/'DogeImage.cpp').open('w') as f:
 f.write('#include "DogeImage.h"\nstatic const uint16_t pixels[] = {\n')
 for i in range(0,len(colors),16): f.write(','.join('DOGE_PIXEL(0x%04x)'%c for c in colors[i:i+16])+',\n')
 f.write('};\nlv_img_dsc_t doge_image = {};\nvoid doge_image_init() {\n doge_image.header.w=280; doge_image.header.h=280; doge_image.header.cf=LV_IMG_CF_TRUE_COLOR;\n doge_image.data_size=sizeof(pixels); doge_image.data=(const uint8_t*)pixels;\n}\n')
(dst/'DogeImage.h').write_text('''#pragma once
#include <lvgl.h>
static_assert(LV_COLOR_DEPTH == 16, "Doge uses RGB565 artwork");
#if LV_COLOR_16_SWAP
#define DOGE_PIXEL(x) uint16_t(((x)>>8)|((x)<<8))
#else
#define DOGE_PIXEL(x) x
#endif
extern lv_img_dsc_t doge_image;
void doge_image_init();
''')
with wave.open(str(dst/'assets/bark.wav')) as w:
 assert w.getframerate()==16000 and w.getsampwidth()==2 and w.getnchannels()==1
 pcm=struct.unpack('<%dh'%w.getnframes(),w.readframes(w.getnframes()))
 assert max(abs(x) for x in pcm)>1000
(dst/'BarkClip.h').write_text('#pragma once\n#include <stdint.h>\n#include <stddef.h>\nconstexpr size_t BARK_SAMPLE_COUNT = %d;\nextern const int16_t bark_pcm[BARK_SAMPLE_COUNT];\n'%len(pcm))
with (dst/'BarkClip.cpp').open('w') as f:
 f.write('#include "BarkClip.h"\nconst int16_t bark_pcm[BARK_SAMPLE_COUNT] = {\n')
 for i in range(0,len(pcm),24): f.write(','.join(map(str,pcm[i:i+24]))+',\n')
 f.write('};\n')
# Retain the exact hardware initialization and DMA flushing proven on this V2 board.
old=(src/'VoiceRecorder.cpp').read_text()
functions=old[old.index('bool codec_ok'):old.index('bool play_chime')]
(dst/'BarkAudio.cpp').write_text('''#include "BarkAudio.h"
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
''' + functions + '''
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
''')
(dst/'BarkAudio.h').write_text('''#pragma once
enum class BarkState { Starting, Ready, Playing, Error };
void bark_begin();
bool bark_request();
BarkState bark_state();
''')
print('Created image and bark assets:',len(colors),'pixels;',len(pcm),'audio samples')
