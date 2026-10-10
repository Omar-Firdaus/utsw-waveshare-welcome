#include "LVGL_Driver.h"
#include "DogeImage.h"
#include "BarkAudio.h"

static lv_obj_t *dog, *caption;
static uint32_t woof_until = 0;
static uint32_t spin_last_ms = 0, spin_phase_ms = 0;
constexpr uint32_t SPIN_PERIOD_MS = 6000;

static void tapped(lv_event_t *) {
  if (bark_request()) {
    woof_until = millis() + 750;
    lv_label_set_text(caption, "Woof!");
    lv_img_set_zoom(dog, 266);
  }
}

static void update_ui(lv_timer_t *) {
  // LVGL angles use tenths of a degree. Elapsed time keeps the rotation
  // steady even when a frame takes longer, including across millis rollover.
  uint32_t now = millis();
  spin_phase_ms = (spin_phase_ms + (now - spin_last_ms) % SPIN_PERIOD_MS) % SPIN_PERIOD_MS;
  spin_last_ms = now;
  lv_img_set_angle(dog, spin_phase_ms * 3600 / SPIN_PERIOD_MS);
  BarkState state = bark_state();
  if (state == BarkState::Error) {
    lv_label_set_text(caption, "Audio error - restart");
    lv_img_set_zoom(dog, 256);
  } else if (state == BarkState::Starting) {
    lv_label_set_text(caption, "Waking up...");
  } else if (state == BarkState::Playing || int32_t(woof_until-millis()) > 0) {
    lv_label_set_text(caption, "Woof!");
  } else {
    lv_label_set_text(caption, "");
    lv_img_set_zoom(dog, 256);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("Doge Bark V2 build 1");
  I2C_Init();
  TCA9554PWR_Init(0x00);
  Backlight_Init();
  Set_Backlight(0);
  LCD_Init();
  Lvgl_Init();
  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFF4D6), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  doge_image_init();
  dog = lv_img_create(screen);
  lv_img_set_src(dog, &doge_image);
  lv_obj_align(dog, LV_ALIGN_CENTER, 0, 0);
  lv_img_set_pivot(dog, 180, 180);
  lv_img_set_antialias(dog, true);
  lv_obj_add_flag(dog, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(dog, tapped, LV_EVENT_CLICKED, nullptr);
  caption = lv_label_create(screen);
  lv_label_set_text(caption, "Waking up...");
  lv_obj_set_style_text_color(caption, lv_color_white(), 0);
  lv_obj_set_style_shadow_color(caption, lv_color_black(), 0);
  lv_obj_set_style_shadow_width(caption, 8, 0);
  lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(caption, LV_ALIGN_CENTER, 0, 130);
  bark_begin();
  spin_last_ms = millis();
  lv_timer_create(update_ui, 33, nullptr);
  lv_refr_now(nullptr);
  Set_Backlight(80);
  Serial.println("Doge screen ready");
}

void loop() {
  Lvgl_Loop();
  delay(5);
}
