#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_UP = 0,
    BUTTON_DOWN,
    BUTTON_CONFIRM,
    BUTTON_COUNT,
} button_id_t;

typedef enum {
    BUTTON_EVENT_PRESS = 0,
    BUTTON_EVENT_LONG_PRESS,
} button_event_t;

typedef void (*buttons_callback_t)(button_id_t button, button_event_t event, void *user_ctx);

typedef struct {
    gpio_num_t up_pin;
    gpio_num_t down_pin;
    gpio_num_t confirm_pin;
    uint32_t poll_period_ms;
    uint32_t debounce_count;
    buttons_callback_t callback;
    void *user_ctx;
} buttons_config_t;

esp_err_t buttons_init(const buttons_config_t *config);
const char *buttons_get_name(button_id_t button);
const char *buttons_get_event_name(button_event_t event);

#ifdef __cplusplus
}
#endif
