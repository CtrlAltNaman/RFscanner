#include "buttons.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "buttons";

typedef struct {
    buttons_config_t config;
    gpio_num_t pins[BUTTON_COUNT];
    uint32_t stable_count[BUTTON_COUNT];
    uint8_t last_sample[BUTTON_COUNT];
    uint8_t stable_state[BUTTON_COUNT];
    bool confirm_long_fired;
    TickType_t confirm_press_tick;
} buttons_context_t;

static buttons_context_t s_ctx;

static const char *BUTTON_NAMES[BUTTON_COUNT] = {
    "UP",
    "DOWN",
    "CONFIRM",
};

static const char *BUTTON_EVENT_NAMES[] = {
    "PRESS",
    "LONG_PRESS",
};

static void buttons_task(void *arg)
{
    buttons_context_t *ctx = (buttons_context_t *)arg;
    const TickType_t long_press_window = pdMS_TO_TICKS(700);

    while (true) {
        for (int i = 0; i < BUTTON_COUNT; ++i) {
            const uint8_t sample = (gpio_get_level(ctx->pins[i]) == 0) ? 1U : 0U;

            if (sample == ctx->last_sample[i]) {
                if (ctx->stable_count[i] < ctx->config.debounce_count) {
                    ctx->stable_count[i]++;
                }
            } else {
                ctx->stable_count[i] = 0;
                ctx->last_sample[i] = sample;
            }

            if ((ctx->stable_count[i] >= ctx->config.debounce_count) &&
                (sample != ctx->stable_state[i])) {
                ctx->stable_state[i] = sample;

                if ((sample == 1U) && (ctx->config.callback != NULL)) {
                    if (i == BUTTON_CONFIRM) {
                        ctx->confirm_press_tick = xTaskGetTickCount();
                        ctx->confirm_long_fired = false;
                    } else {
                        ctx->config.callback((button_id_t)i, BUTTON_EVENT_PRESS, ctx->config.user_ctx);
                    }
                } else if ((sample == 0U) && (ctx->config.callback != NULL) && (i == BUTTON_CONFIRM)) {
                    if (!ctx->confirm_long_fired) {
                        ctx->config.callback(BUTTON_CONFIRM, BUTTON_EVENT_PRESS, ctx->config.user_ctx);
                    }
                    ctx->confirm_long_fired = false;
                }
            }
        }

        if (ctx->stable_state[BUTTON_CONFIRM] &&
            !ctx->confirm_long_fired &&
            (ctx->config.callback != NULL) &&
            ((xTaskGetTickCount() - ctx->confirm_press_tick) >= long_press_window)) {
            ctx->confirm_long_fired = true;
            ctx->config.callback(BUTTON_CONFIRM, BUTTON_EVENT_LONG_PRESS, ctx->config.user_ctx);
        }

        vTaskDelay(pdMS_TO_TICKS(ctx->config.poll_period_ms));
    }
}

esp_err_t buttons_init(const buttons_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.config = *config;
    s_ctx.pins[BUTTON_UP] = config->up_pin;
    s_ctx.pins[BUTTON_DOWN] = config->down_pin;
    s_ctx.pins[BUTTON_CONFIRM] = config->confirm_pin;

    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << config->up_pin) |
                        (1ULL << config->down_pin) |
                        (1ULL << config->confirm_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "Failed to configure buttons");

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        const uint8_t sample = (gpio_get_level(s_ctx.pins[i]) == 0) ? 1U : 0U;
        s_ctx.last_sample[i] = sample;
        s_ctx.stable_state[i] = sample;
    }

    BaseType_t task_ok = xTaskCreate(buttons_task, "buttons_task", 3072, &s_ctx, 5, NULL);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to create buttons task");

    ESP_LOGI(TAG, "Buttons task started");
    return ESP_OK;
}

const char *buttons_get_name(button_id_t button)
{
    if (((int)button < 0) || ((int)button >= BUTTON_COUNT)) {
        return "UNKNOWN";
    }

    return BUTTON_NAMES[button];
}

const char *buttons_get_event_name(button_event_t event)
{
    if (((int)event < 0) || ((int)event >= (int)(sizeof(BUTTON_EVENT_NAMES) / sizeof(BUTTON_EVENT_NAMES[0])))) {
        return "UNKNOWN";
    }

    return BUTTON_EVENT_NAMES[event];
}
