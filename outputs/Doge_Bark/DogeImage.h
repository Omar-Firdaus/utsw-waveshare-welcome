#pragma once
#include <lvgl.h>
static_assert(LV_COLOR_DEPTH == 16, "Doge uses RGB565 artwork");
#if LV_COLOR_16_SWAP
#define DOGE_PIXEL(x) uint16_t(((x)>>8)|((x)<<8))
#else
#define DOGE_PIXEL(x) x
#endif
extern lv_img_dsc_t doge_image;
void doge_image_init();
