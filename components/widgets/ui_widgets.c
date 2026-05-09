#include "ui_widgets.h"

#include "ui_animations.h"

uint16_t ui_widgets_theme_accent(void)
{
    const app_settings_t settings = app_system_get_settings();

    switch (settings.color_theme % 3U) {
    case 1:
        return TFT_COLOR_CYAN;
    case 2:
        return TFT_COLOR_RED;
    default:
        return TFT_COLOR_GREEN_BRIGHT;
    }
}

uint16_t ui_widgets_theme_dim(void)
{
    const app_settings_t settings = app_system_get_settings();

    switch (settings.color_theme % 3U) {
    case 1:
        return TFT_COLOR_BLUE;
    case 2:
        return TFT_COLOR_RED;
    default:
        return TFT_COLOR_GREEN_DIM;
    }
}

uint16_t ui_widgets_theme_alt(void)
{
    const app_settings_t settings = app_system_get_settings();

    switch (settings.color_theme % 3U) {
    case 1:
        return TFT_COLOR_GREEN_BRIGHT;
    case 2:
        return TFT_COLOR_YELLOW;
    default:
        return TFT_COLOR_CYAN;
    }
}

void ui_widgets_draw_header(tft_display_t *display, const char *title)
{
    const uint16_t dim = ui_widgets_theme_dim();
    tft_display_fill_rect(display, 0, 0, 160, 12, dim);
    tft_display_draw_text(display, 4, 2, title, TFT_COLOR_BLACK, dim, 1);
}

void ui_widgets_draw_status_line(tft_display_t *display, int y, const char *label, const char *value, uint16_t color)
{
    tft_display_fill_rect(display, 0, y, 160, 8, TFT_COLOR_BLACK);
    tft_display_draw_text(display, 4, y, label, ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 86, y, value, color, TFT_COLOR_BLACK, 1);
}

void ui_widgets_draw_bar(tft_display_t *display, int x, int y, int w, int h, uint8_t percent, uint16_t color)
{
    tft_display_fill_rect(display, x, y, w, h, ui_widgets_theme_dim());
    tft_display_fill_rect(display, x + 1, y + 1, w - 2, h - 2, TFT_COLOR_BLACK);

    const int fill_width = ((w - 2) * percent) / 100;
    if (fill_width > 0) {
        tft_display_fill_rect(display, x + 1, y + 1, fill_width, h - 2, color);
    }
}

void ui_widgets_draw_radar_background(tft_display_t *display, uint32_t tick_ms)
{
    const uint16_t sweep_x = ui_animations_radar_x(tick_ms, 160);
    const uint16_t dim = ui_widgets_theme_dim();
    const uint16_t accent = ui_widgets_theme_accent();

    for (int y = 14; y < 128; y += 10) {
        tft_display_fill_rect(display, 0, y, 160, 1, dim);
    }
    tft_display_fill_rect(display, sweep_x, 14, 2, 110, accent);
}

void ui_widgets_draw_history_graph(tft_display_t *display, int x, int y, int w, int h, const uint8_t *values, int count, uint16_t color)
{
    tft_display_fill_rect(display, x, y, w, h, ui_widgets_theme_dim());
    tft_display_fill_rect(display, x + 1, y + 1, w - 2, h - 2, TFT_COLOR_BLACK);

    if (count <= 0) {
        return;
    }

    const int bar_w = (w - 2) / count;
    for (int i = 0; i < count; ++i) {
        const int bar_h = ((h - 2) * values[i]) / 100;
        tft_display_fill_rect(display, x + 1 + (i * bar_w), y + h - 1 - bar_h, bar_w - 1, bar_h, color);
    }
}

void ui_widgets_draw_waveform(tft_display_t *display, int x, int y, int w, int h, const uint16_t *values, int count, uint16_t color)
{
    tft_display_fill_rect(display, x, y, w, h, ui_widgets_theme_dim());
    tft_display_fill_rect(display, x + 1, y + 1, w - 2, h - 2, TFT_COLOR_BLACK);

    if (count <= 0) {
        return;
    }

    const int col_w = (w - 2) / count;
    for (int i = 0; i < count; ++i) {
        const int pulse_h = ((h - 2) * ((values[i] > 255U) ? 255U : values[i])) / 255;
        tft_display_fill_rect(display, x + 1 + (i * col_w), y + h - 1 - pulse_h, 1, pulse_h, color);
    }
}

const char *ui_widgets_direction_text(app_direction_t direction)
{
    switch (direction) {
    case APP_DIRECTION_LEFT:
        return "LEFT";
    case APP_DIRECTION_RIGHT:
        return "RIGHT";
    case APP_DIRECTION_TOP:
        return "TOP";
    default:
        return "NONE";
    }
}
