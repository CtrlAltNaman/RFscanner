#include "app_system.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "app_utils.h"

#define APP_MENU_COUNT 6
#define APP_ANALYZER_PAGE_COUNT 3
#define APP_SETTINGS_COUNT 6

static const char *TAG = "app_system";

typedef struct {
    app_snapshot_t snapshot;
    SemaphoreHandle_t mutex;
} app_system_ctx_t;

static app_system_ctx_t s_ctx;

const char *app_system_mode_name(app_mode_t mode)
{
    switch (mode) {
    case APP_MODE_RF_SCAN:
        return "RF";
    case APP_MODE_CSI:
        return "CSI";
    case APP_MODE_ANALYZER:
        return "ANALYZER";
    case APP_MODE_STATUS:
        return "STATUS";
    case APP_MODE_SETTINGS:
        return "SETTINGS";
    case APP_MODE_ABOUT:
        return "ABOUT";
    default:
        return "MENU";
    }
}

static void app_system_apply_mode_defaults_locked(app_mode_t mode)
{
    s_ctx.snapshot.mode = mode;

    if (mode == APP_MODE_MAIN_MENU) {
        s_ctx.snapshot.analyzer_page = 0;
        s_ctx.snapshot.settings_index = 0;
        s_ctx.snapshot.rf.paused = false;
        s_ctx.snapshot.csi.paused = false;
    }
}

static void app_system_log_mode_transition(app_mode_t from, app_mode_t to)
{
    if (from != to) {
        ESP_LOGI(TAG, "Mode transition: %s -> %s",
                 app_system_mode_name(from),
                 app_system_mode_name(to));
    }
}

static esp_err_t app_system_load_settings_locked(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rfsurv", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t size = sizeof(s_ctx.snapshot.settings);
    err = nvs_get_blob(handle, "settings", &s_ctx.snapshot.settings, &size);
    nvs_close(handle);
    return err;
}

esp_err_t app_system_save_settings(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rfsurv", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    err = nvs_set_blob(handle, "settings", &s_ctx.snapshot.settings, sizeof(s_ctx.snapshot.settings));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    xSemaphoreGive(s_ctx.mutex);

    nvs_close(handle);
    return err;
}

static void app_system_monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        s_ctx.snapshot.uptime_ms += 250U;
        s_ctx.snapshot.free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        if ((s_ctx.snapshot.mode == APP_MODE_BOOT) && (s_ctx.snapshot.uptime_ms >= 4200U)) {
            app_system_apply_mode_defaults_locked(APP_MODE_MAIN_MENU);
        }
        xSemaphoreGive(s_ctx.mutex);

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t app_system_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_ctx.snapshot.mode = APP_MODE_BOOT;
    s_ctx.snapshot.menu_index = 0;
    s_ctx.snapshot.settings_index = 0;
    s_ctx.snapshot.analyzer_page = 0;
    strlcpy(s_ctx.snapshot.last_button, "NONE", sizeof(s_ctx.snapshot.last_button));

    s_ctx.snapshot.settings.brightness_pct = 85U;
    s_ctx.snapshot.settings.animation_speed_pct = 70U;
    s_ctx.snapshot.settings.color_theme = 0U;
    s_ctx.snapshot.settings.scan_dwell_ms = 12U;
    s_ctx.snapshot.settings.smoothing_pct = 35U;
    s_ctx.snapshot.settings.sound_enabled = false;
    s_ctx.snapshot.csi.wifi_channel = 6U;
    s_ctx.snapshot.rf.view_mode = APP_RF_VIEW_BARS;

    if (app_system_load_settings_locked() != ESP_OK) {
        ESP_LOGW(TAG, "Using default settings");
    }

    return ESP_OK;
}

esp_err_t app_system_start_monitor_task(void)
{
    BaseType_t ok = xTaskCreate(app_system_monitor_task, "sys_monitor", 4096, NULL, 3, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_system_get_snapshot(app_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    *snapshot = s_ctx.snapshot;
    xSemaphoreGive(s_ctx.mutex);
}

app_mode_t app_system_get_mode(void)
{
    app_mode_t mode;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    mode = s_ctx.snapshot.mode;
    xSemaphoreGive(s_ctx.mutex);
    return mode;
}

void app_system_set_mode(app_mode_t mode)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    const app_mode_t previous_mode = s_ctx.snapshot.mode;
    app_system_apply_mode_defaults_locked(mode);
    const app_mode_t new_mode = s_ctx.snapshot.mode;
    xSemaphoreGive(s_ctx.mutex);
    app_system_log_mode_transition(previous_mode, new_mode);
}

void app_system_set_last_button(const char *name)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    strlcpy(s_ctx.snapshot.last_button, name, sizeof(s_ctx.snapshot.last_button));
    xSemaphoreGive(s_ctx.mutex);
}

void app_system_handle_button(button_id_t button, button_event_t event)
{
    bool save_settings = false;
    app_mode_t new_mode;

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    const app_mode_t previous_mode = s_ctx.snapshot.mode;

    if (s_ctx.snapshot.mode == APP_MODE_BOOT) {
        app_system_apply_mode_defaults_locked(APP_MODE_MAIN_MENU);
        xSemaphoreGive(s_ctx.mutex);
        app_system_log_mode_transition(previous_mode, APP_MODE_MAIN_MENU);
        return;
    }

    if ((button == BUTTON_CONFIRM) && (event == BUTTON_EVENT_LONG_PRESS) &&
        (s_ctx.snapshot.mode != APP_MODE_MAIN_MENU)) {
        app_system_apply_mode_defaults_locked(APP_MODE_MAIN_MENU);
        xSemaphoreGive(s_ctx.mutex);
        app_system_log_mode_transition(previous_mode, APP_MODE_MAIN_MENU);
        return;
    }

    switch (s_ctx.snapshot.mode) {
    case APP_MODE_MAIN_MENU:
        if ((button == BUTTON_UP) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.menu_index = app_utils_wrap_index(s_ctx.snapshot.menu_index - 1, APP_MENU_COUNT);
        } else if ((button == BUTTON_DOWN) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.menu_index = app_utils_wrap_index(s_ctx.snapshot.menu_index + 1, APP_MENU_COUNT);
        } else if ((button == BUTTON_CONFIRM) && (event == BUTTON_EVENT_PRESS)) {
            static const app_mode_t modes[APP_MENU_COUNT] = {
                APP_MODE_RF_SCAN, APP_MODE_CSI, APP_MODE_ANALYZER,
                APP_MODE_STATUS, APP_MODE_SETTINGS, APP_MODE_ABOUT,
            };
            app_system_apply_mode_defaults_locked(modes[s_ctx.snapshot.menu_index]);
        }
        break;

    case APP_MODE_RF_SCAN:
        if ((button == BUTTON_CONFIRM) && (event == BUTTON_EVENT_PRESS)) {
            if (s_ctx.snapshot.rf.paused) {
                s_ctx.snapshot.rf.paused = false;
                ESP_LOGI(TAG, "RF action: resume");
            } else if (s_ctx.snapshot.rf.scan_mode == APP_RF_MODE_SCAN) {
                s_ctx.snapshot.rf.scan_mode = APP_RF_MODE_TRACK;
                s_ctx.snapshot.rf.current_channel = s_ctx.snapshot.rf.strongest_channel;
                ESP_LOGI(TAG, "RF action: switch to TRACK on ch=%u", s_ctx.snapshot.rf.current_channel);
            } else {
                s_ctx.snapshot.rf.scan_mode = APP_RF_MODE_SCAN;
                ESP_LOGI(TAG, "RF action: switch to SCAN");
            }
        } else if ((button == BUTTON_UP) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.rf.view_mode = APP_RF_VIEW_BARS;
            ESP_LOGI(TAG, "RF action: view=BARS");
        } else if ((button == BUTTON_DOWN) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.rf.view_mode = APP_RF_VIEW_RADAR;
            ESP_LOGI(TAG, "RF action: view=RADAR");
        }
        break;

    case APP_MODE_CSI:
        if ((button == BUTTON_CONFIRM) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.csi.paused = !s_ctx.snapshot.csi.paused;
        } else if ((button == BUTTON_UP) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.csi.wifi_channel = app_utils_clamp_u8((int)s_ctx.snapshot.csi.wifi_channel + 1, 1, 13);
        } else if ((button == BUTTON_DOWN) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.csi.wifi_channel = app_utils_clamp_u8((int)s_ctx.snapshot.csi.wifi_channel - 1, 1, 13);
        }
        break;

    case APP_MODE_ANALYZER:
        if ((button == BUTTON_CONFIRM) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.analyzer_page =
                app_utils_wrap_index(s_ctx.snapshot.analyzer_page + 1, APP_ANALYZER_PAGE_COUNT);
        } else if ((button == BUTTON_UP) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.analyzer_page =
                app_utils_wrap_index(s_ctx.snapshot.analyzer_page - 1, APP_ANALYZER_PAGE_COUNT);
        } else if ((button == BUTTON_DOWN) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.analyzer_page =
                app_utils_wrap_index(s_ctx.snapshot.analyzer_page + 1, APP_ANALYZER_PAGE_COUNT);
        }
        break;

    case APP_MODE_STATUS:
    case APP_MODE_ABOUT:
        break;

    case APP_MODE_SETTINGS:
        if ((button == BUTTON_UP) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.settings_index = app_utils_wrap_index(s_ctx.snapshot.settings_index - 1, APP_SETTINGS_COUNT);
        } else if ((button == BUTTON_DOWN) && (event == BUTTON_EVENT_PRESS)) {
            s_ctx.snapshot.settings_index = app_utils_wrap_index(s_ctx.snapshot.settings_index + 1, APP_SETTINGS_COUNT);
        } else if ((button == BUTTON_CONFIRM) && (event == BUTTON_EVENT_PRESS)) {
            switch (s_ctx.snapshot.settings_index) {
            case 0:
                s_ctx.snapshot.settings.brightness_pct = app_utils_clamp_u8((int)s_ctx.snapshot.settings.brightness_pct + 5, 10, 100);
                if (s_ctx.snapshot.settings.brightness_pct >= 100U) {
                    s_ctx.snapshot.settings.brightness_pct = 10U;
                }
                break;
            case 1:
                s_ctx.snapshot.settings.animation_speed_pct = app_utils_clamp_u8((int)s_ctx.snapshot.settings.animation_speed_pct + 5, 20, 100);
                if (s_ctx.snapshot.settings.animation_speed_pct >= 100U) {
                    s_ctx.snapshot.settings.animation_speed_pct = 20U;
                }
                break;
            case 2:
            {
                int dwell = (int)s_ctx.snapshot.settings.scan_dwell_ms + 2;
                if (dwell > 40) {
                    dwell = 2;
                }
                s_ctx.snapshot.settings.scan_dwell_ms = (uint16_t)dwell;
                break;
            }
            case 3:
                s_ctx.snapshot.settings.smoothing_pct = app_utils_clamp_u8((int)s_ctx.snapshot.settings.smoothing_pct + 5, 5, 95);
                if (s_ctx.snapshot.settings.smoothing_pct >= 95U) {
                    s_ctx.snapshot.settings.smoothing_pct = 5U;
                }
                break;
            case 4:
                s_ctx.snapshot.settings.color_theme = (uint8_t)((s_ctx.snapshot.settings.color_theme + 1U) % 3U);
                break;
            case 5:
                s_ctx.snapshot.settings.sound_enabled = !s_ctx.snapshot.settings.sound_enabled;
                break;
            default:
                break;
            }
            save_settings = true;
        }
        break;

    default:
        break;
    }

    new_mode = s_ctx.snapshot.mode;
    xSemaphoreGive(s_ctx.mutex);
    app_system_log_mode_transition(previous_mode, new_mode);

    if (save_settings) {
        (void)app_system_save_settings();
    }
}

void app_system_set_display_ok(bool ok)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.display_ok = ok;
    xSemaphoreGive(s_ctx.mutex);
}

void app_system_set_spi_ok(bool ok)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.spi_ok = ok;
    xSemaphoreGive(s_ctx.mutex);
}

void app_system_set_buttons_ok(bool ok)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.buttons_ok = ok;
    xSemaphoreGive(s_ctx.mutex);
}

void app_system_set_nrf_status(nrf24_position_t position, bool ok)
{
    if ((position < 0) || (position >= NRF24_POSITION_COUNT)) {
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.nrf_ok[position] = ok;
    xSemaphoreGive(s_ctx.mutex);
}

void app_system_set_rf_state(const app_rf_state_t *state)
{
    if (state == NULL) {
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    const bool paused = s_ctx.snapshot.rf.paused;
    const app_rf_mode_t scan_mode = s_ctx.snapshot.rf.scan_mode;
    const app_rf_view_t view_mode = s_ctx.snapshot.rf.view_mode;
    s_ctx.snapshot.rf = *state;
    s_ctx.snapshot.rf.paused = paused;
    s_ctx.snapshot.rf.scan_mode = scan_mode;
    s_ctx.snapshot.rf.view_mode = view_mode;
    xSemaphoreGive(s_ctx.mutex);
}

void app_system_set_csi_state(const app_csi_state_t *state)
{
    if (state == NULL) {
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.csi = *state;
    xSemaphoreGive(s_ctx.mutex);
}

void app_system_set_wifi_active(bool active)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.wifi_active = active;
    xSemaphoreGive(s_ctx.mutex);
}

void app_system_set_fps(uint16_t fps)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.snapshot.fps = fps;
    xSemaphoreGive(s_ctx.mutex);
}

app_settings_t app_system_get_settings(void)
{
    app_settings_t settings;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    settings = s_ctx.snapshot.settings;
    xSemaphoreGive(s_ctx.mutex);
    return settings;
}
