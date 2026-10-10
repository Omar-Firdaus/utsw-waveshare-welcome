#include "Ui.h"

namespace ui {
namespace {

Action deferred;
bool pending = false;

void on_click(lv_event_t *e) {
  if (pending) return;
  deferred = *static_cast<Action *>(lv_event_get_user_data(e));
  pending = true;
}

void on_delete(lv_event_t *e) { delete static_cast<Action *>(lv_event_get_user_data(e)); }

void plain(lv_obj_t *obj) {
  lv_obj_remove_style_all(obj);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

lv_obj_t *screen() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, lv_color_hex(BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  return scr;
}

lv_obj_t *overlay() {
  lv_obj_t *panel = lv_obj_create(lv_scr_act());
  plain(panel);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(PANEL), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  return panel;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color,
                lv_coord_t width, lv_coord_t x, lv_coord_t y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, width);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, x, y);
  return l;
}

lv_obj_t *column(lv_obj_t *parent, lv_coord_t width, lv_coord_t y) {
  lv_obj_t *c = lv_obj_create(parent);
  plain(c);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(c, width, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(c, 12, 0);
  lv_obj_align(c, LV_ALIGN_CENTER, 0, y);
  return c;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, lv_coord_t w, lv_coord_t h, lv_coord_t x, lv_coord_t y,
                 Kind kind, Action action, const lv_font_t *font) {
  uint32_t bg = 0xFFFFFF, fg = BG;
  if (kind == Kind::Secondary) { bg = 0x2A6DB5; fg = TEXT; }
  if (kind == Kind::Danger) { bg = 0xC62828; fg = TEXT; }
  if (kind == Kind::Good) { bg = 0x2E7D32; fg = TEXT; }
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_align(b, LV_ALIGN_CENTER, x, y);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_STATE_DISABLED);
  lv_obj_set_style_text_color(b, lv_color_hex(fg), 0);
  lv_obj_set_style_text_opa(b, LV_OPA_50, LV_STATE_DISABLED);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_center(l);
  Action *stored = new Action(std::move(action));
  lv_obj_add_event_cb(b, on_click, LV_EVENT_CLICKED, stored);
  lv_obj_add_event_cb(b, on_delete, LV_EVENT_DELETE, stored);
  return b;
}

void set_enabled(lv_obj_t *button, bool enabled) {
  if (!button) return;
  if (enabled) lv_obj_clear_state(button, LV_STATE_DISABLED);
  else lv_obj_add_state(button, LV_STATE_DISABLED);
}

void run_deferred() {
  if (!pending) return;
  Action action = std::move(deferred);
  deferred = nullptr;
  pending = false;
  if (action) action();
}

}  // namespace ui
