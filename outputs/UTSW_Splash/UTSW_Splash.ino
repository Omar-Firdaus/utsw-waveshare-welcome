#include "LVGL_Driver.h"
#include "utsw_logo.h"
#include "VoiceRecorder.h"
#include <math.h>

enum class Page { Home, Welcome, Screensaver };
static Page page = Page::Home;
static lv_img_dsc_t logo;
static lv_obj_t *start_button, *status_label, *voice_bars[7], *dots[3];
static uint32_t welcome_finished_before = 0;
static bool welcome_complete = false;
static float voice_energy = 0;

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int y) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, y);
  return label;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int y) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_size(button, 220, 52);
  lv_obj_align(button, LV_ALIGN_CENTER, 0, y);
  lv_obj_set_style_radius(button, 26, 0);
  lv_obj_set_style_bg_color(button, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xD9E8F7), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_text_color(button, lv_color_hex(0x004C97), 0);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

static void draw_logo(lv_obj_t *screen) {
  lv_obj_t *image = lv_img_create(screen);
  lv_img_set_src(image, &logo);
  lv_obj_set_style_img_recolor(image, lv_color_white(), 0);
  lv_obj_set_style_img_recolor_opa(image, LV_OPA_COVER, 0);
  lv_obj_center(image);
}

static void screensaver_event(lv_event_t *) {
  if (!welcome_complete || page != Page::Welcome) return;
  page = Page::Screensaver;
  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  start_button = nullptr;
  draw_logo(screen);
  for (int i = 0; i < 3; ++i) {
    dots[i] = lv_obj_create(screen);
    lv_obj_remove_style_all(dots[i]);
    lv_obj_set_size(dots[i], 7, 7);
    lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dots[i], lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
    lv_obj_align(dots[i], LV_ALIGN_CENTER, (i-1)*20, 85);
    lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_CLICKABLE);
  }
  Serial.println("Welcome completed; screensaver active");
}

static void welcome_event(lv_event_t *) {
  if (page != Page::Home) return;
  welcome_finished_before = recorder_welcome_finished();
  if (!recorder_welcome()) return;
  page = Page::Welcome;
  welcome_complete = false;
  voice_energy = 0;
  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  make_label(screen, "Welcome", -135);
  make_label(screen, "UT Southwestern\nDepartment of Neurology", -101);
  lv_obj_t *orb = lv_obj_create(screen);
  lv_obj_remove_style_all(orb);
  lv_obj_set_size(orb, 140, 140);
  lv_obj_align(orb, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(orb, lv_color_hex(0x155EA6), 0);
  lv_obj_set_style_bg_opa(orb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(orb, 2, 0);
  lv_obj_set_style_border_color(orb, lv_color_hex(0x4482BD), 0);
  lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < 7; ++i) {
    voice_bars[i] = lv_obj_create(orb);
    lv_obj_remove_style_all(voice_bars[i]);
    lv_obj_set_size(voice_bars[i], 10, 10);
    lv_obj_set_style_radius(voice_bars[i], 5, 0);
    lv_obj_set_style_bg_color(voice_bars[i], lv_color_white(), 0);
    lv_obj_set_style_bg_opa(voice_bars[i], LV_OPA_COVER, 0);
    lv_obj_align(voice_bars[i], LV_ALIGN_CENTER, (i-3)*17, 0);
    lv_obj_clear_flag(voice_bars[i], LV_OBJ_FLAG_CLICKABLE);
  }
  status_label = make_label(screen, "Please listen", 90);
  start_button = make_button(screen, "Start", 112);
  lv_obj_add_flag(start_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(start_button, screensaver_event, LV_EVENT_CLICKED, nullptr);
}

static void update_ui(lv_timer_t *) {
  RecorderState state = recorder_state();
  if (page == Page::Home) {
    if (state == RecorderState::Ready) lv_obj_clear_state(start_button, LV_STATE_DISABLED);
    else if (state == RecorderState::Error) lv_label_set_text(lv_obj_get_child(start_button, 0), "Audio unavailable");
    return;
  }
  float t = millis() / 1000.0f;
  if (page == Page::Screensaver) {
    for (int i = 0; i < 3; ++i) {
      int opacity = 90 + int(100 * (0.5f + 0.5f*sinf(t*1.4f-i*0.8f)));
      lv_obj_set_style_bg_opa(dots[i], opacity, 0);
    }
    return;
  }
  float target = state == RecorderState::Welcome ? recorder_voice_level()/32768.0f : 0;
  voice_energy += (target-voice_energy) * (target > voice_energy ? 0.45f : 0.18f);
  for (int i = 0; i < 7; ++i) {
    float shape = 1.0f - 0.15f * abs(i-3);
    float variation = 0.65f + 0.35f * sinf(t*8.0f+i*1.2f);
    int height = 10 + int(82 * sqrtf(voice_energy) * shape * variation);
    lv_obj_set_height(voice_bars[i], height);
    lv_obj_align(voice_bars[i], LV_ALIGN_CENTER, (i-3)*17, 0);
  }
  if (state == RecorderState::Error) {
    lv_label_set_text(status_label, "Audio error - restart device");
    return;
  }
  if (!welcome_complete && recorder_welcome_finished() != welcome_finished_before) {
    welcome_complete = true;
    lv_label_set_text(status_label, "When you're ready");
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 69);
    lv_obj_clear_flag(start_button, LV_OBJ_FLAG_HIDDEN);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("UTSW V2 welcome build 5");
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
  logo.header.h = 56;
  logo.header.cf = LV_IMG_CF_ALPHA_8BIT;
  logo.data_size = sizeof(utsw_logo_alpha);
  logo.data = utsw_logo_alpha;
  draw_logo(screen);
  start_button = make_button(screen, "Start", 86);
  lv_obj_add_state(start_button, LV_STATE_DISABLED);
  lv_obj_add_event_cb(start_button, welcome_event, LV_EVENT_CLICKED, nullptr);
  recorder_begin();
  lv_timer_create(update_ui, 33, nullptr);
  lv_refr_now(NULL);
  Set_Backlight(80);
  Serial.println("Welcome screen ready");
}

void loop() {
  Lvgl_Loop();
  delay(5);
}
