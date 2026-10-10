#pragma once
#include <lvgl.h>
#include <functional>

extern "C" {
extern const lv_font_t recall_font_20;
extern const lv_font_t recall_font_28;
}

namespace ui {

constexpr uint32_t BG = 0x004C97;
constexpr uint32_t PANEL = 0x0B2545;
constexpr uint32_t TEXT = 0xFFFFFF;
constexpr uint32_t MUTED = 0xBFD3EA;
constexpr uint32_t PROMPT = 0xFFD166;
constexpr uint32_t RECORDING = 0xFF5A5A;
constexpr uint32_t WARNING = 0xFFB4A8;

enum class Kind { Primary, Secondary, Danger, Good };
using Action = std::function<void()>;

lv_obj_t *screen();
lv_obj_t *overlay();
lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color,
                lv_coord_t width, lv_coord_t x = 0, lv_coord_t y = 0);
lv_obj_t *column(lv_obj_t *parent, lv_coord_t width, lv_coord_t y);
lv_obj_t *button(lv_obj_t *parent, const char *text, lv_coord_t w, lv_coord_t h, lv_coord_t x, lv_coord_t y,
                 Kind kind, Action action, const lv_font_t *font = &recall_font_20);
void set_enabled(lv_obj_t *button, bool enabled);

// Button actions run here, from loop(), never inside LVGL's event handling.
void run_deferred();

}  // namespace ui
