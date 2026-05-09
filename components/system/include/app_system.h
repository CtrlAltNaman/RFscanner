#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buttons.h"
#include "esp_err.h"
#include "nrf24.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_RF_HISTORY_LEN   32
#define APP_CSI_HISTORY_LEN  48

typedef enum {
    APP_MODE_BOOT = 0,
    APP_MODE_MAIN_MENU,
    APP_MODE_RF_SCAN,
    APP_MODE_CSI,
    APP_MODE_ANALYZER,
    APP_MODE_STATUS,
    APP_MODE_SETTINGS,
    APP_MODE_ABOUT,
} app_mode_t;

typedef enum {
    APP_DIRECTION_UNKNOWN = 0,
    APP_DIRECTION_LEFT,
    APP_DIRECTION_RIGHT,
    APP_DIRECTION_TOP,
} app_direction_t;

typedef enum {
    APP_RF_MODE_SCAN = 0,
    APP_RF_MODE_TRACK,
} app_rf_mode_t;

typedef enum {
    APP_RF_VIEW_BARS = 0,
    APP_RF_VIEW_RADAR,
} app_rf_view_t;

typedef struct {
    uint8_t brightness_pct;
    uint8_t animation_speed_pct;
    uint8_t color_theme;
    uint16_t scan_dwell_ms;
    uint8_t smoothing_pct;
    bool sound_enabled;
} app_settings_t;

typedef struct {
    bool active;
    bool paused;
    app_rf_mode_t scan_mode;
    app_rf_view_t view_mode;
    uint8_t current_channel;
    uint8_t strongest_channel;
    uint8_t strongest_intensity;
    uint8_t left_strength;
    uint8_t right_strength;
    uint8_t top_strength;
    app_direction_t direction;
    uint32_t sweep_count;
    uint8_t occupancy[126];
    uint8_t history[APP_RF_HISTORY_LEN];
    uint8_t history_pos;
} app_rf_state_t;

typedef struct {
    bool active;
    bool paused;
    uint8_t wifi_channel;
    uint32_t packet_count;
    uint16_t amplitude;
    uint16_t activity;
    uint16_t waveform[APP_CSI_HISTORY_LEN];
    uint8_t waveform_pos;
} app_csi_state_t;

typedef struct {
    app_mode_t mode;
    bool display_ok;
    bool spi_ok;
    bool buttons_ok;
    bool nrf_ok[NRF24_POSITION_COUNT];
    uint32_t uptime_ms;
    size_t free_heap;
    uint16_t fps;
    bool wifi_active;
    int menu_index;
    int analyzer_page;
    int settings_index;
    char last_button[16];
    app_settings_t settings;
    app_rf_state_t rf;
    app_csi_state_t csi;
} app_snapshot_t;

esp_err_t app_system_init(void);
esp_err_t app_system_start_monitor_task(void);
void app_system_get_snapshot(app_snapshot_t *snapshot);
app_mode_t app_system_get_mode(void);
const char *app_system_mode_name(app_mode_t mode);
void app_system_set_mode(app_mode_t mode);
void app_system_handle_button(button_id_t button, button_event_t event);
void app_system_set_last_button(const char *name);
void app_system_set_display_ok(bool ok);
void app_system_set_spi_ok(bool ok);
void app_system_set_buttons_ok(bool ok);
void app_system_set_nrf_status(nrf24_position_t position, bool ok);
void app_system_set_rf_state(const app_rf_state_t *state);
void app_system_set_csi_state(const app_csi_state_t *state);
void app_system_set_wifi_active(bool active);
void app_system_set_fps(uint16_t fps);
app_settings_t app_system_get_settings(void);
esp_err_t app_system_save_settings(void);

#ifdef __cplusplus
}
#endif
