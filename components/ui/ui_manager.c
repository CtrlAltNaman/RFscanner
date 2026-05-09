#include "ui_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "app_system.h"
#include "ui_screens.h"

static const char *TAG = "ui_manager";

static tft_display_t *s_display;

static void ui_manager_task(void *arg)
{
    (void)arg;
    uint32_t frame_counter = 0U;
    int64_t last_fps_stamp = esp_timer_get_time();
    app_mode_t last_mode = (app_mode_t)-1;
    int last_menu_index = -1;
    int last_settings_index = -1;
    int last_analyzer_page = -1;
    char last_button[16] = {0};
    uint32_t last_boot_bucket = UINT32_MAX;
    uint32_t last_live_bucket = UINT32_MAX;
    uint8_t last_rf_channel = 0xFF;
    uint8_t last_rf_peak_channel = 0xFF;
    uint8_t last_rf_strength = 0xFF;
    uint8_t last_rf_left = 0xFF;
    uint8_t last_rf_right = 0xFF;
    uint8_t last_rf_top = 0xFF;
    app_rf_mode_t last_rf_mode = (app_rf_mode_t)-1;
    app_rf_view_t last_rf_view = (app_rf_view_t)-1;
    app_direction_t last_rf_direction = APP_DIRECTION_UNKNOWN;
    bool last_rf_paused = false;
    uint32_t last_csi_packets = UINT32_MAX;
    uint16_t last_csi_amplitude = UINT16_MAX;
    uint8_t last_csi_activity = 0xFF;
    uint8_t last_csi_channel = 0xFF;
    bool last_csi_paused = false;
    size_t last_free_heap = 0U;
    uint16_t last_fps = 0U;
    bool last_wifi_active = false;
    uint8_t last_brightness = 0xFF;
    uint8_t last_anim_speed = 0xFF;
    uint8_t last_scan_dwell = 0xFF;
    uint8_t last_smoothing = 0xFF;
    uint8_t last_theme = 0xFF;
    bool last_sound = false;

    while (true) {
        app_snapshot_t snapshot = {0};
        app_system_get_snapshot(&snapshot);
        const bool mode_changed = (snapshot.mode != last_mode);
        const bool rf_view_changed = (snapshot.rf.view_mode != last_rf_view);

        bool should_redraw = false;
        const uint32_t boot_bucket = snapshot.uptime_ms / 500U;
        const uint32_t live_bucket = snapshot.uptime_ms / 150U;
        if (mode_changed) {
            should_redraw = true;
        } else if (snapshot.mode == APP_MODE_BOOT) {
            should_redraw = (boot_bucket != last_boot_bucket);
        } else if (snapshot.mode == APP_MODE_MAIN_MENU) {
            should_redraw = (snapshot.menu_index != last_menu_index) ||
                            (strncmp(snapshot.last_button, last_button, sizeof(last_button)) != 0);
        } else if (snapshot.mode == APP_MODE_RF_SCAN) {
            const bool rf_changed = (snapshot.rf.current_channel != last_rf_channel) ||
                            (snapshot.rf.strongest_channel != last_rf_peak_channel) ||
                            (snapshot.rf.strongest_intensity != last_rf_strength) ||
                            (snapshot.rf.left_strength != last_rf_left) ||
                            (snapshot.rf.right_strength != last_rf_right) ||
                            (snapshot.rf.top_strength != last_rf_top) ||
                            (snapshot.rf.scan_mode != last_rf_mode) ||
                            (snapshot.rf.view_mode != last_rf_view) ||
                            (snapshot.rf.direction != last_rf_direction) ||
                            (strncmp(snapshot.last_button, last_button, sizeof(last_button)) != 0) ||
                            (snapshot.rf.paused != last_rf_paused);
            should_redraw = rf_changed && (live_bucket != last_live_bucket);
        } else if (snapshot.mode == APP_MODE_CSI) {
            const bool csi_changed = (snapshot.csi.packet_count != last_csi_packets) ||
                            (snapshot.csi.amplitude != last_csi_amplitude) ||
                            (snapshot.csi.activity != last_csi_activity) ||
                            (snapshot.csi.wifi_channel != last_csi_channel) ||
                            (strncmp(snapshot.last_button, last_button, sizeof(last_button)) != 0) ||
                            (snapshot.csi.paused != last_csi_paused);
            should_redraw = csi_changed && (live_bucket != last_live_bucket);
        } else if (snapshot.mode == APP_MODE_ANALYZER) {
            const bool analyzer_changed = (snapshot.analyzer_page != last_analyzer_page) ||
                            (snapshot.rf.strongest_intensity != last_rf_strength) ||
                            (snapshot.csi.activity != last_csi_activity) ||
                            (snapshot.rf.direction != last_rf_direction);
            should_redraw = analyzer_changed && (live_bucket != last_live_bucket);
        } else if (snapshot.mode == APP_MODE_SETTINGS) {
            should_redraw = (snapshot.settings_index != last_settings_index) ||
                            (snapshot.settings.brightness_pct != last_brightness) ||
                            (snapshot.settings.animation_speed_pct != last_anim_speed) ||
                            (snapshot.settings.scan_dwell_ms != last_scan_dwell) ||
                            (snapshot.settings.smoothing_pct != last_smoothing) ||
                            (snapshot.settings.color_theme != last_theme) ||
                            (snapshot.settings.sound_enabled != last_sound);
        } else {
            should_redraw = (strncmp(snapshot.last_button, last_button, sizeof(last_button)) != 0) ||
                            (snapshot.free_heap != last_free_heap) ||
                            (snapshot.fps != last_fps) ||
                            (snapshot.wifi_active != last_wifi_active);
        }

        (void)tft_display_set_backlight_percent(s_display, snapshot.settings.brightness_pct);
        if (should_redraw) {
            ui_screens_render(s_display,
                              &snapshot,
                              (uint32_t)(snapshot.uptime_ms * snapshot.settings.animation_speed_pct / 100U),
                              mode_changed || rf_view_changed);
        }

        last_mode = snapshot.mode;
        last_menu_index = snapshot.menu_index;
        last_settings_index = snapshot.settings_index;
        last_analyzer_page = snapshot.analyzer_page;
        last_boot_bucket = boot_bucket;
        last_live_bucket = live_bucket;
        last_rf_channel = snapshot.rf.current_channel;
        last_rf_peak_channel = snapshot.rf.strongest_channel;
        last_rf_strength = snapshot.rf.strongest_intensity;
        last_rf_left = snapshot.rf.left_strength;
        last_rf_right = snapshot.rf.right_strength;
        last_rf_top = snapshot.rf.top_strength;
        last_rf_mode = snapshot.rf.scan_mode;
        last_rf_view = snapshot.rf.view_mode;
        last_rf_direction = snapshot.rf.direction;
        last_rf_paused = snapshot.rf.paused;
        last_csi_packets = snapshot.csi.packet_count;
        last_csi_amplitude = snapshot.csi.amplitude;
        last_csi_activity = (uint8_t)((snapshot.csi.activity > 255U) ? 255U : snapshot.csi.activity);
        last_csi_channel = snapshot.csi.wifi_channel;
        last_csi_paused = snapshot.csi.paused;
        last_free_heap = snapshot.free_heap;
        last_fps = snapshot.fps;
        last_wifi_active = snapshot.wifi_active;
        last_brightness = snapshot.settings.brightness_pct;
        last_anim_speed = snapshot.settings.animation_speed_pct;
        last_scan_dwell = (uint8_t)((snapshot.settings.scan_dwell_ms > 255U) ? 255U : snapshot.settings.scan_dwell_ms);
        last_smoothing = snapshot.settings.smoothing_pct;
        last_theme = snapshot.settings.color_theme;
        last_sound = snapshot.settings.sound_enabled;
        strlcpy(last_button, snapshot.last_button, sizeof(last_button));

        frame_counter++;
        const int64_t now = esp_timer_get_time();
        if ((now - last_fps_stamp) >= 1000000LL) {
            app_system_set_fps((uint16_t)frame_counter);
            frame_counter = 0U;
            last_fps_stamp = now;
        }

        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

esp_err_t ui_manager_init(tft_display_t *display)
{
    s_display = display;
    return (s_display != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t ui_manager_start_task(void)
{
    BaseType_t ok = xTaskCreate(ui_manager_task, "ui_task", 6144, NULL, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to start UI task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
