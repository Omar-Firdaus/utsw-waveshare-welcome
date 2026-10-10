from pathlib import Path
from PIL import Image
import shutil
src=Path('/Users/omarfirdaus/Downloads/ESP32-S3-Touch-LCD-1.85C-Demo/Arduino/examples/LVGL_Arduino')
out=Path('outputs/UTSW_Splash'); out.mkdir(exist_ok=True)
for stem in ['Display_ST77916','LVGL_Driver','I2C_Driver','TCA9554PWR','Touch_CST816','esp_lcd_st77916']:
 for p in src.glob(stem+'.*'): shutil.copy2(p,out/p.name)
# Convert the official artwork to an LVGL alpha bitmap; retain aspect ratio.
im=Image.open('work/utsw-white.png').convert('RGBA')
im=im.crop(im.getchannel('A').getbbox())
im=im.resize((280,round(280*im.height/im.width)),Image.Resampling.LANCZOS)
a=im.getchannel('A').tobytes()
(out/'utsw_logo.h').write_text('#pragma once\n#include <lvgl.h>\nstatic const uint8_t utsw_logo_alpha[] = {\n'+','.join(str(x) for x in a)+'\n};\n')
(out/'UTSW_Splash.ino').write_text('''#include "LVGL_Driver.h"
#include "utsw_logo.h"

static lv_img_dsc_t logo;

void setup() {
  Serial.begin(115200);
  I2C_Init();
  TCA9554PWR_Init(0x00);
  Backlight_Init();
  Set_Backlight(0);
  LCD_Init();
  Lvgl_Init();

  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x004C97), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  logo.header.always_zero = 0;
  logo.header.w = 280;
  logo.header.h = HEIGHT;
  logo.header.cf = LV_IMG_CF_ALPHA_8BIT;
  logo.data_size = sizeof(utsw_logo_alpha);
  logo.data = utsw_logo_alpha;
  lv_obj_t *image = lv_img_create(screen);
  lv_img_set_src(image, &logo);
  lv_obj_set_style_img_recolor(image, lv_color_white(), 0);
  lv_obj_set_style_img_recolor_opa(image, LV_OPA_COVER, 0);
  lv_obj_center(image);
  lv_refr_now(NULL);
  Set_Backlight(80);
  Serial.println("UTSW splash ready");
}

void loop() {
  Lvgl_Loop();
  delay(5);
}
'''.replace('HEIGHT',str(im.height)))
(out/'README.md').write_text('''# UT Southwestern splash screen
Open UTSW_Splash.ino in Arduino IDE.

Board: ESP32S3 Dev Module; ESP32 core 3.1.1; Flash Size 16MB; PSRAM OPI; USB CDC On Boot Enabled; USB Mode Hardware CDC and JTAG; partition 16M Flash (3MB APP/9.9MB FATFS).
Requires the Waveshare bundled LVGL 8.3.10 already installed on this Mac.
Display drivers copied from the working Waveshare 1.85C demo.
Official logo: https://brand.utswmed.org/documents/1378/master-logo-reversed-white.png
Brand source: https://brand.utswmed.org/ut-southwestern-logos/
Background: #004C97. Centered logo: 280 pixels wide, original proportions.
''')
# Layout preview for the physical round display.
preview=Image.new('RGBA',(360,360),(0,76,151,255)); preview.alpha_composite(im,((360-im.width)//2,(360-im.height)//2))
from PIL import ImageDraw
mask=Image.new('L',(360,360)); ImageDraw.Draw(mask).ellipse((0,0,359,359),fill=255); preview.putalpha(mask)
preview.save('outputs/UTSW-preview.png')
print('Created',out, 'logo:',im.size)
