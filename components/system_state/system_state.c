#include "system_state.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_check.h"

typedef struct {
    system_state_snapshot_t snapshot;
    SemaphoreHandle_t mutex;
} system_state_context_t;

static system_state_context_t s_ctx;

esp_err_t system_state_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_ctx.mutex != NULL, ESP_ERR_NO_MEM, "system_state", "Failed to create mutex");

    strlcpy(s_ctx.snapshot.last_button, "NONE", sizeof(s_ctx.snapshot.last_button));
    return ESP_OK;
}

void system_state_set_display_ready(bool ready)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.display_ready = ready;
    xSemaphoreGive(s_ctx.mutex);
}

void system_state_set_nrf_status(nrf24_position_t position, bool ok)
{
    if ((position < 0) || (position >= NRF24_POSITION_COUNT)) {
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.nrf_ok[position] = ok;
    xSemaphoreGive(s_ctx.mutex);
}

void system_state_set_last_button(const char *button_name)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    strlcpy(s_ctx.snapshot.last_button, button_name, sizeof(s_ctx.snapshot.last_button));
    xSemaphoreGive(s_ctx.mutex);
}

void system_state_toggle_menu_mode(void)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.menu_mode = !s_ctx.snapshot.menu_mode;
    xSemaphoreGive(s_ctx.mutex);
}

bool system_state_is_menu_mode(void)
{
    bool menu_mode = false;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    menu_mode = s_ctx.snapshot.menu_mode;
    xSemaphoreGive(s_ctx.mutex);
    return menu_mode;
}

void system_state_move_menu(int delta, int item_count)
{
    if (item_count <= 0) {
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.menu_index += delta;

    if (s_ctx.snapshot.menu_index < 0) {
        s_ctx.snapshot.menu_index = item_count - 1;
    } else if (s_ctx.snapshot.menu_index >= item_count) {
        s_ctx.snapshot.menu_index = 0;
    }

    xSemaphoreGive(s_ctx.mutex);
}

void system_state_get_snapshot(system_state_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    *snapshot = s_ctx.snapshot;
    xSemaphoreGive(s_ctx.mutex);
}
