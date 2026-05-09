#include "tft_display.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "spi_manager.h"

#define TFT_CMD_SWRESET 0x01
#define TFT_CMD_SLPOUT  0x11
#define TFT_CMD_DISPON  0x29
#define TFT_CMD_CASET   0x2A
#define TFT_CMD_RASET   0x2B
#define TFT_CMD_RAMWR   0x2C
#define TFT_CMD_MADCTL  0x36
#define TFT_CMD_COLMOD  0x3A
#define TFT_CMD_FRMCTR1 0xB1
#define TFT_CMD_FRMCTR2 0xB2
#define TFT_CMD_FRMCTR3 0xB3
#define TFT_CMD_INVCTR  0xB4
#define TFT_CMD_PWCTR1  0xC0
#define TFT_CMD_PWCTR2  0xC1
#define TFT_CMD_PWCTR3  0xC2
#define TFT_CMD_PWCTR4  0xC3
#define TFT_CMD_PWCTR5  0xC4
#define TFT_CMD_VMCTR1  0xC5
#define TFT_CMD_GMCTRP1 0xE0
#define TFT_CMD_GMCTRN1 0xE1

static const char *TAG = "tft_display";

/*
 * This bring-up driver is tuned for a common ST7735 SPI TFT.
 * If your exact module uses different offsets or another controller variant,
 * we can keep the rest of the project unchanged and adjust only this file.
 */

typedef struct {
    char character;
    uint8_t columns[5];
} glyph_t;

static const glyph_t FONT[] = {
    { '%', {0x19, 0x19, 0x02, 0x13, 0x13} },
    { '+', {0x04, 0x0E, 0x04, 0x00, 0x00} },
    { '.', {0x00, 0x10, 0x00, 0x00, 0x00} },
    { '/', {0x18, 0x04, 0x03, 0x00, 0x00} },
    { '0', {0x0E, 0x19, 0x15, 0x13, 0x0E} },
    { '1', {0x12, 0x1F, 0x10, 0x00, 0x00} },
    { '2', {0x19, 0x15, 0x15, 0x12, 0x00} },
    { '3', {0x11, 0x15, 0x15, 0x0A, 0x00} },
    { '4', {0x07, 0x04, 0x04, 0x1F, 0x04} },
    { '5', {0x17, 0x15, 0x15, 0x09, 0x00} },
    { '6', {0x0E, 0x15, 0x15, 0x08, 0x00} },
    { '7', {0x01, 0x01, 0x1D, 0x03, 0x00} },
    { '8', {0x0A, 0x15, 0x15, 0x0A, 0x00} },
    { '9', {0x02, 0x15, 0x15, 0x0E, 0x00} },
    { ':', {0x00, 0x0A, 0x00, 0x00, 0x00} },
    { '=', {0x0A, 0x0A, 0x0A, 0x00, 0x00} },
    { ' ', {0x00, 0x00, 0x00, 0x00, 0x00} },
    { '-', {0x08, 0x08, 0x08, 0x08, 0x08} },
    { '>', {0x00, 0x11, 0x0A, 0x04, 0x00} },
    { 'A', {0x1E, 0x05, 0x05, 0x1E, 0x00} },
    { 'B', {0x1F, 0x15, 0x15, 0x0A, 0x00} },
    { 'C', {0x0E, 0x11, 0x11, 0x0A, 0x00} },
    { 'D', {0x1F, 0x11, 0x11, 0x0E, 0x00} },
    { 'E', {0x1F, 0x15, 0x15, 0x11, 0x00} },
    { 'F', {0x1F, 0x05, 0x05, 0x01, 0x00} },
    { 'G', {0x0E, 0x11, 0x15, 0x1D, 0x00} },
    { 'H', {0x1F, 0x04, 0x04, 0x1F, 0x00} },
    { 'I', {0x11, 0x1F, 0x11, 0x00, 0x00} },
    { 'J', {0x08, 0x10, 0x10, 0x0F, 0x00} },
    { 'K', {0x1F, 0x04, 0x0A, 0x11, 0x00} },
    { 'L', {0x1F, 0x10, 0x10, 0x10, 0x00} },
    { 'M', {0x1F, 0x02, 0x04, 0x02, 0x1F} },
    { 'N', {0x1F, 0x02, 0x04, 0x1F, 0x00} },
    { 'O', {0x0E, 0x11, 0x11, 0x0E, 0x00} },
    { 'P', {0x1F, 0x05, 0x05, 0x02, 0x00} },
    { 'Q', {0x0E, 0x11, 0x19, 0x1E, 0x00} },
    { 'R', {0x1F, 0x05, 0x0D, 0x12, 0x00} },
    { 'S', {0x12, 0x15, 0x15, 0x09, 0x00} },
    { 'T', {0x01, 0x1F, 0x01, 0x00, 0x00} },
    { 'U', {0x0F, 0x10, 0x10, 0x0F, 0x00} },
    { 'V', {0x07, 0x08, 0x10, 0x08, 0x07} },
    { 'W', {0x1F, 0x08, 0x04, 0x08, 0x1F} },
    { 'X', {0x11, 0x0A, 0x04, 0x0A, 0x11} },
    { 'Y', {0x03, 0x04, 0x18, 0x04, 0x03} },
    { 'Z', {0x19, 0x15, 0x13, 0x00, 0x00} },
};

static const glyph_t *find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); ++i) {
        if (FONT[i].character == c) {
            return &FONT[i];
        }
    }

    return &FONT[0];
}

static esp_err_t tft_send(tft_display_t *display, const void *data, size_t size, int dc_level)
{
    spi_transaction_t transaction = {
        .length = size * 8,
        .tx_buffer = data,
        .user = (void *)(intptr_t)dc_level,
    };

    spi_manager_lock(portMAX_DELAY);
    gpio_set_level(display->dc_pin, dc_level);
    const esp_err_t err = spi_device_polling_transmit(display->spi_handle, &transaction);
    spi_manager_unlock();
    return err;
}

static esp_err_t tft_write_command(tft_display_t *display, uint8_t command)
{
    return tft_send(display, &command, 1, 0);
}

static esp_err_t tft_write_data(tft_display_t *display, const void *data, size_t size)
{
    return tft_send(display, data, size, 1);
}

static esp_err_t tft_set_window(tft_display_t *display, int x0, int y0, int x1, int y1)
{
    uint8_t column_data[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF),
    };
    uint8_t row_data[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF),
    };

    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_CASET), TAG, "CASET failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, column_data, sizeof(column_data)), TAG, "Column write failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_RASET), TAG, "RASET failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, row_data, sizeof(row_data)), TAG, "Row write failed");
    return tft_write_command(display, TFT_CMD_RAMWR);
}

static esp_err_t tft_set_backlight(tft_display_t *display, uint32_t duty)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, display->backlight_channel, duty),
                        TAG, "ledc_set_duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, display->backlight_channel);
}

esp_err_t tft_display_init(tft_display_t *display, const tft_display_config_t *config)
{
    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "display is null");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    memset(display, 0, sizeof(*display));
    display->dc_pin = config->dc_pin;
    display->reset_pin = config->reset_pin;
    display->backlight_pin = config->backlight_pin;
    display->width = config->width;
    display->height = config->height;
    display->backlight_channel = LEDC_CHANNEL_0;

    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << config->dc_pin) | (1ULL << config->reset_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "Failed to configure TFT control pins");

    spi_device_interface_config_t spi_config = {
        .clock_speed_hz = 4 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = config->cs_pin,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_manager_add_device(&spi_config, &display->spi_handle),
                        TAG, "Failed to add TFT to SPI bus");

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&ledc_timer), TAG, "LEDC timer config failed");

    ledc_channel_config_t ledc_channel = {
        .gpio_num = config->backlight_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = display->backlight_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ledc_channel), TAG, "LEDC channel config failed");

    gpio_set_level(display->reset_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(display->reset_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_SWRESET), TAG, "SWRESET failed");
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_SLPOUT), TAG, "SLPOUT failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    const uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    const uint8_t invctr = 0x07;
    const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    const uint8_t pwctr2 = 0xC5;
    const uint8_t pwctr3[] = {0x0A, 0x00};
    const uint8_t pwctr4[] = {0x8A, 0x2A};
    const uint8_t pwctr5[] = {0x8A, 0xEE};
    const uint8_t vmctr1 = 0x0E;
    const uint8_t gmctrp1[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                               0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    const uint8_t gmctrn1[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                               0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};

    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_FRMCTR1), TAG, "FRMCTR1 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, frmctr1, sizeof(frmctr1)), TAG, "FRMCTR1 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_FRMCTR2), TAG, "FRMCTR2 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, frmctr2, sizeof(frmctr2)), TAG, "FRMCTR2 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_FRMCTR3), TAG, "FRMCTR3 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, frmctr3, sizeof(frmctr3)), TAG, "FRMCTR3 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_INVCTR), TAG, "INVCTR failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, &invctr, sizeof(invctr)), TAG, "INVCTR data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_PWCTR1), TAG, "PWCTR1 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, pwctr1, sizeof(pwctr1)), TAG, "PWCTR1 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_PWCTR2), TAG, "PWCTR2 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, &pwctr2, sizeof(pwctr2)), TAG, "PWCTR2 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_PWCTR3), TAG, "PWCTR3 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, pwctr3, sizeof(pwctr3)), TAG, "PWCTR3 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_PWCTR4), TAG, "PWCTR4 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, pwctr4, sizeof(pwctr4)), TAG, "PWCTR4 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_PWCTR5), TAG, "PWCTR5 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, pwctr5, sizeof(pwctr5)), TAG, "PWCTR5 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_VMCTR1), TAG, "VMCTR1 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, &vmctr1, sizeof(vmctr1)), TAG, "VMCTR1 data failed");

    uint8_t pixel_format = 0x05;
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_COLMOD), TAG, "COLMOD failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, &pixel_format, sizeof(pixel_format)), TAG, "COLMOD data failed");
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t madctl = 0xC0;
    if (config->rotation == TFT_ROTATION_90) {
        madctl = 0xA0;
    } else if (config->rotation == TFT_ROTATION_180) {
        madctl = 0x00;
    } else if (config->rotation == TFT_ROTATION_270) {
        madctl = 0x60;
    }
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_MADCTL), TAG, "MADCTL failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, &madctl, sizeof(madctl)), TAG, "MADCTL data failed");

    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_GMCTRP1), TAG, "GMCTRP1 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, gmctrp1, sizeof(gmctrp1)), TAG, "GMCTRP1 data failed");
    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_GMCTRN1), TAG, "GMCTRN1 failed");
    ESP_RETURN_ON_ERROR(tft_write_data(display, gmctrn1, sizeof(gmctrn1)), TAG, "GMCTRN1 data failed");

    ESP_RETURN_ON_ERROR(tft_write_command(display, TFT_CMD_DISPON), TAG, "DISPON failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(tft_set_backlight(display, 1023), TAG, "Backlight enable failed");
    ESP_RETURN_ON_ERROR(tft_display_clear(display, TFT_COLOR_BLACK), TAG, "Initial clear failed");

    ESP_LOGI(TAG, "TFT initialized");
    return ESP_OK;
}

esp_err_t tft_display_clear(tft_display_t *display, uint16_t color)
{
    return tft_display_fill_rect(display, 0, 0, display->width, display->height, color);
}

esp_err_t tft_display_fill_rect(tft_display_t *display, int x, int y, int w, int h, uint16_t color)
{
    if ((w <= 0) || (h <= 0)) {
        return ESP_OK;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if ((x + w) > display->width) {
        w = display->width - x;
    }
    if ((y + h) > display->height) {
        h = display->height - y;
    }

    if ((w <= 0) || (h <= 0)) {
        return ESP_OK;
    }

    uint8_t line_buffer[160 * 2];
    for (int i = 0; i < w; ++i) {
        line_buffer[i * 2] = (uint8_t)(color >> 8);
        line_buffer[i * 2 + 1] = (uint8_t)(color & 0xFF);
    }

    ESP_RETURN_ON_ERROR(tft_set_window(display, x, y, x + w - 1, y + h - 1), TAG, "Window set failed");
    for (int row = 0; row < h; ++row) {
        ESP_RETURN_ON_ERROR(tft_write_data(display, line_buffer, w * 2), TAG, "Fill row failed");
    }

    return ESP_OK;
}

static esp_err_t tft_draw_char(tft_display_t *display,
                               int x,
                               int y,
                               char c,
                               uint16_t fg_color,
                               uint16_t bg_color,
                               uint8_t scale)
{
    const glyph_t *glyph = find_glyph(c);

    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            const bool pixel_on = ((glyph->columns[col] >> row) & 0x01U) != 0U;
            const uint16_t color = pixel_on ? fg_color : bg_color;
            ESP_RETURN_ON_ERROR(tft_display_fill_rect(display,
                                                      x + col * scale,
                                                      y + row * scale,
                                                      scale,
                                                      scale,
                                                      color),
                                TAG, "Char draw failed");
        }
    }

    return tft_display_fill_rect(display, x + 5 * scale, y, scale, 7 * scale, bg_color);
}

esp_err_t tft_display_draw_text(tft_display_t *display,
                                int x,
                                int y,
                                const char *text,
                                uint16_t fg_color,
                                uint16_t bg_color,
                                uint8_t scale)
{
    ESP_RETURN_ON_FALSE(text != NULL, ESP_ERR_INVALID_ARG, TAG, "text is null");

    int cursor_x = x;
    for (size_t i = 0; i < strlen(text); ++i) {
        ESP_RETURN_ON_ERROR(tft_draw_char(display, cursor_x, y, text[i], fg_color, bg_color, scale),
                            TAG, "Failed to draw '%c'", text[i]);
        cursor_x += 6 * scale;
    }

    return ESP_OK;
}

esp_err_t tft_display_backlight_fade_test(tft_display_t *display, uint8_t cycles, uint32_t step_delay_ms)
{
    for (uint8_t cycle = 0; cycle < cycles; ++cycle) {
        for (uint32_t duty = 0; duty <= 1023; duty += 32) {
            ESP_RETURN_ON_ERROR(tft_set_backlight(display, duty), TAG, "Fade up failed");
            vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
        }
        for (int duty = 1023; duty >= 0; duty -= 32) {
            ESP_RETURN_ON_ERROR(tft_set_backlight(display, (uint32_t)duty), TAG, "Fade down failed");
            vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
        }
    }

    return tft_set_backlight(display, 1023);
}

esp_err_t tft_display_set_backlight_percent(tft_display_t *display, uint8_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }

    return tft_set_backlight(display, (1023U * percent) / 100U);
}
