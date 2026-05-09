#pragma once

#include <stdint.h>

#include "app_system.h"
#include "tft_display.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_widgets_draw_header(tft_display_t *display, const char *title);
void ui_widgets_draw_status_line(tft_display_t *display, int y, const char *label, const char *value, uint16_t color);
void ui_widgets_draw_bar(tft_display_t *display, int x, int y, int w, int h, uint8_t percent, uint16_t color);
void ui_widgets_draw_radar_background(tft_display_t *display, uint32_t tick_ms);
void ui_widgets_draw_history_graph(tft_display_t *display, int x, int y, int w, int h, const uint8_t *values, int count, uint16_t color);
void ui_widgets_draw_waveform(tft_display_t *display, int x, int y, int w, int h, const uint16_t *values, int count, uint16_t color);
const char *ui_widgets_direction_text(app_direction_t direction);
uint16_t ui_widgets_theme_accent(void);
uint16_t ui_widgets_theme_dim(void);
uint16_t ui_widgets_theme_alt(void);

#ifdef __cplusplus
}
#endif
