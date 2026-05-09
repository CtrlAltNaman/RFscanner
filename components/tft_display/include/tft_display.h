#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TFT_COLOR_BLACK 0x0000
#define TFT_COLOR_WHITE 0xFFFF
#define TFT_COLOR_RED 0xF800
#define TFT_COLOR_GREEN 0x07E0
#define TFT_COLOR_BLUE 0x001F
#define TFT_COLOR_YELLOW 0xFFE0
#define TFT_COLOR_CYAN 0x07FF
#define TFT_COLOR_GREEN_DIM 0x03E0
#define TFT_COLOR_GREEN_BRIGHT 0x87F0

typedef enum {
    TFT_ROTATION_0 = 0,
    TFT_ROTATION_90,
    TFT_ROTATION_180,
    TFT_ROTATION_270,
} tft_rotation_t;

typedef struct {
    gpio_num_t cs_pin;
    gpio_num_t dc_pin;
    gpio_num_t reset_pin;
    gpio_num_t backlight_pin;
    uint16_t width;
    uint16_t height;
    tft_rotation_t rotation;
} tft_display_config_t;

typedef struct {
    spi_device_handle_t spi_handle;
    gpio_num_t dc_pin;
    gpio_num_t reset_pin;
    gpio_num_t backlight_pin;
    uint16_t width;
    uint16_t height;
    ledc_channel_t backlight_channel;
} tft_display_t;

esp_err_t tft_display_init(tft_display_t *display, const tft_display_config_t *config);
esp_err_t tft_display_clear(tft_display_t *display, uint16_t color);
esp_err_t tft_display_fill_rect(tft_display_t *display, int x, int y, int w, int h, uint16_t color);
esp_err_t tft_display_draw_text(tft_display_t *display,
                                int x,
                                int y,
                                const char *text,
                                uint16_t fg_color,
                                uint16_t bg_color,
                                uint8_t scale);
esp_err_t tft_display_backlight_fade_test(tft_display_t *display, uint8_t cycles, uint32_t step_delay_ms);
esp_err_t tft_display_set_backlight_percent(tft_display_t *display, uint8_t percent);

#ifdef __cplusplus
}
#endif
