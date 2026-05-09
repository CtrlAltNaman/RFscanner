#include "rf_scanner.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "app_system.h"
#include "app_utils.h"

static const char *TAG = "rf_scanner";

typedef struct {
    rf_scanner_config_t config;
    app_rf_state_t state;
    bool initialized;
} rf_ctx_t;

static rf_ctx_t s_rf;

#define RF_SCAN_MIN_DIRECTION_STRENGTH 6U
#define RF_TRACK_MIN_DIRECTION_STRENGTH 5U
#define RF_SCAN_DIRECTION_MARGIN  3U
#define RF_TRACK_DIRECTION_MARGIN 2U
#define RF_SCAN_DIRECTION_STREAK_REQ 3U
#define RF_TRACK_DIRECTION_STREAK_REQ 2U
#define RF_TRACK_MIN_LOOPS        12U

static const char *rf_direction_text(app_direction_t direction)
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

static const char *rf_mode_text(app_rf_mode_t mode)
{
    return (mode == APP_RF_MODE_TRACK) ? "TRACK" : "SCAN";
}

static uint8_t rf_strength_from_counts(uint32_t hits, uint32_t samples)
{
    if (samples == 0U) {
        return 0U;
    }

    return (uint8_t)((hits * 100U) / samples);
}

static app_direction_t rf_raw_direction(uint8_t left,
                                        uint8_t right,
                                        uint8_t top,
                                        app_rf_mode_t mode,
                                        uint8_t *strongest_out,
                                        uint8_t *second_out)
{
    uint8_t strengths[NRF24_POSITION_COUNT] = { left, right, top };
    app_direction_t directions[NRF24_POSITION_COUNT] = {
        APP_DIRECTION_LEFT,
        APP_DIRECTION_RIGHT,
        APP_DIRECTION_TOP,
    };

    uint8_t strongest = strengths[0];
    uint8_t second = 0U;
    app_direction_t best = directions[0];

    for (int i = 1; i < NRF24_POSITION_COUNT; ++i) {
        if (strengths[i] > strongest) {
            second = strongest;
            strongest = strengths[i];
            best = directions[i];
        } else if (strengths[i] > second) {
            second = strengths[i];
        }
    }

    if (strongest_out != NULL) {
        *strongest_out = strongest;
    }
    if (second_out != NULL) {
        *second_out = second;
    }

    const uint8_t min_strength = (mode == APP_RF_MODE_TRACK) ?
                                 RF_TRACK_MIN_DIRECTION_STRENGTH :
                                 RF_SCAN_MIN_DIRECTION_STRENGTH;
    const uint8_t margin = (mode == APP_RF_MODE_TRACK) ?
                           RF_TRACK_DIRECTION_MARGIN :
                           RF_SCAN_DIRECTION_MARGIN;

    if (strongest < min_strength) {
        return APP_DIRECTION_UNKNOWN;
    }
    if ((strongest - second) < margin) {
        return APP_DIRECTION_UNKNOWN;
    }

    return best;
}

static app_direction_t rf_stabilize_direction(app_direction_t candidate,
                                              app_direction_t previous,
                                              app_rf_mode_t mode,
                                              uint8_t strongest,
                                              uint8_t second,
                                              uint8_t *streak)
{
    const uint8_t min_strength = (mode == APP_RF_MODE_TRACK) ?
                                 RF_TRACK_MIN_DIRECTION_STRENGTH :
                                 RF_SCAN_MIN_DIRECTION_STRENGTH;
    const uint8_t streak_req = (mode == APP_RF_MODE_TRACK) ?
                               RF_TRACK_DIRECTION_STREAK_REQ :
                               RF_SCAN_DIRECTION_STREAK_REQ;

    if (candidate == APP_DIRECTION_UNKNOWN) {
        if ((previous != APP_DIRECTION_UNKNOWN) &&
            (strongest >= min_strength) &&
            ((strongest - second) >= 1U)) {
            return previous;
        }

        *streak = 0U;
        return APP_DIRECTION_UNKNOWN;
    }

    if (candidate == previous) {
        if (*streak < 255U) {
            (*streak)++;
        }
        return previous;
    }

    if ((previous != APP_DIRECTION_UNKNOWN) && (strongest >= min_strength)) {
        *streak = 0U;
        return candidate;
    }

    if (*streak + 1U < streak_req) {
        (*streak)++;
        return previous;
    }

    *streak = 0U;
    return candidate;
}

static uint32_t rf_sample_hits(nrf24_device_t *device)
{
    bool rpd = false;
    uint8_t status = 0;
    uint32_t hits = 0U;

    if ((nrf24_read_rpd(device, &rpd) == ESP_OK) && rpd) {
        hits++;
    }
    if ((nrf24_read_status(device, &status) == ESP_OK) && ((status & 0x40U) != 0U)) {
        hits += 2U;
    }
    (void)nrf24_clear_status(device);

    return hits;
}

static void rf_scanner_task(void *arg)
{
    (void)arg;
    float ema_left = 0.0f;
    float ema_right = 0.0f;
    float ema_top = 0.0f;
    app_direction_t stable_direction = APP_DIRECTION_UNKNOWN;
    uint8_t direction_streak = 0U;

    while (true) {
        app_snapshot_t snapshot = {0};
        app_system_get_snapshot(&snapshot);
        const app_mode_t mode = snapshot.mode;
        const app_settings_t settings = app_system_get_settings();
        const bool active = (mode == APP_MODE_RF_SCAN) || (mode == APP_MODE_ANALYZER);
        s_rf.state.paused = snapshot.rf.paused;
        s_rf.state.scan_mode = snapshot.rf.scan_mode;

        if (!active || s_rf.state.paused) {
            s_rf.state.active = active;
            (void)nrf24_set_ce(s_rf.config.left, false);
            (void)nrf24_set_ce(s_rf.config.right, false);
            (void)nrf24_set_ce(s_rf.config.top, false);
            app_system_set_rf_state(&s_rf.state);
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        uint32_t sample_loops = (settings.scan_dwell_ms < 2U) ? 2U : (settings.scan_dwell_ms / 2U);
        const float alpha = ((float)settings.smoothing_pct) / 100.0f;

        if (s_rf.state.scan_mode == APP_RF_MODE_TRACK && sample_loops < RF_TRACK_MIN_LOOPS) {
            sample_loops = RF_TRACK_MIN_LOOPS;
        }

        s_rf.state.active = true;
        if (s_rf.state.scan_mode == APP_RF_MODE_SCAN) {
            s_rf.state.current_channel = (uint8_t)((s_rf.state.current_channel + 1U) % 126U);
            if (s_rf.state.current_channel == 0U) {
                s_rf.state.current_channel = 1U;
            }
        } else {
            if ((s_rf.state.strongest_channel == 0U) || (s_rf.state.strongest_channel > 125U)) {
                s_rf.state.strongest_channel = (s_rf.state.current_channel == 0U) ? 1U : s_rf.state.current_channel;
            }
            s_rf.state.current_channel = s_rf.state.strongest_channel;
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.left, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.right, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.top, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_channel(s_rf.config.left, s_rf.state.current_channel));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_channel(s_rf.config.right, s_rf.state.current_channel));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_channel(s_rf.config.top, s_rf.state.current_channel));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_flush_rx(s_rf.config.left));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_flush_rx(s_rf.config.right));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_flush_rx(s_rf.config.top));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_clear_status(s_rf.config.left));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_clear_status(s_rf.config.right));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_clear_status(s_rf.config.top));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.left, true));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.right, true));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.top, true));
        vTaskDelay(pdMS_TO_TICKS(2));

        uint32_t left_hits = 0U;
        uint32_t right_hits = 0U;
        uint32_t top_hits = 0U;
        for (uint32_t i = 0; i < sample_loops; ++i) {
            left_hits += rf_sample_hits(s_rf.config.left);
            right_hits += rf_sample_hits(s_rf.config.right);
            top_hits += rf_sample_hits(s_rf.config.top);
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.left, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.right, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nrf24_set_ce(s_rf.config.top, false));

        ema_left = app_utils_ema(ema_left, (float)rf_strength_from_counts(left_hits, sample_loops * 2U), alpha);
        ema_right = app_utils_ema(ema_right, (float)rf_strength_from_counts(right_hits, sample_loops * 2U), alpha);
        ema_top = app_utils_ema(ema_top, (float)rf_strength_from_counts(top_hits, sample_loops * 2U), alpha);

        s_rf.state.left_strength = (uint8_t)ema_left;
        s_rf.state.right_strength = (uint8_t)ema_right;
        s_rf.state.top_strength = (uint8_t)ema_top;

        uint8_t strongest = 0U;
        uint8_t second = 0U;
        const app_direction_t candidate_direction =
            rf_raw_direction(s_rf.state.left_strength,
                             s_rf.state.right_strength,
                             s_rf.state.top_strength,
                             s_rf.state.scan_mode,
                             &strongest,
                             &second);
        stable_direction = rf_stabilize_direction(candidate_direction,
                                                  stable_direction,
                                                  s_rf.state.scan_mode,
                                                  strongest,
                                                  second,
                                                  &direction_streak);
        s_rf.state.strongest_intensity = strongest;
        s_rf.state.direction = stable_direction;
        const uint8_t strongest_index = (s_rf.state.strongest_channel > 0U) ? (s_rf.state.strongest_channel - 1U) : 0U;
        if (s_rf.state.scan_mode == APP_RF_MODE_SCAN) {
            s_rf.state.occupancy[s_rf.state.current_channel - 1U] = s_rf.state.strongest_intensity;
            s_rf.state.strongest_channel =
                (s_rf.state.strongest_intensity >= s_rf.state.occupancy[strongest_index]) ? s_rf.state.current_channel : s_rf.state.strongest_channel;
        }
        s_rf.state.history[s_rf.state.history_pos] = s_rf.state.strongest_intensity;
        s_rf.state.history_pos = (uint8_t)((s_rf.state.history_pos + 1U) % APP_RF_HISTORY_LEN);
        s_rf.state.sweep_count++;

        ESP_LOGI(TAG,
                 "rf mode=%s ch=%u dwell=%ums loops=%lu left=%u right=%u top=%u peak=%u second=%u cand=%s dir=%s sweep=%lu",
                 rf_mode_text(s_rf.state.scan_mode),
                 s_rf.state.current_channel,
                 (unsigned)settings.scan_dwell_ms,
                 (unsigned long)sample_loops,
                 s_rf.state.left_strength,
                 s_rf.state.right_strength,
                 s_rf.state.top_strength,
                 s_rf.state.strongest_intensity,
                 second,
                 rf_direction_text(candidate_direction),
                 rf_direction_text(s_rf.state.direction),
                 (unsigned long)s_rf.state.sweep_count);

        app_system_set_rf_state(&s_rf.state);
        vTaskDelay(pdMS_TO_TICKS(settings.scan_dwell_ms));
    }
}

esp_err_t rf_scanner_init(const rf_scanner_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    memset(&s_rf, 0, sizeof(s_rf));
    s_rf.config = *config;
    s_rf.state.scan_mode = APP_RF_MODE_SCAN;
    s_rf.state.current_channel = 1U;
    s_rf.state.strongest_channel = 1U;

    ESP_RETURN_ON_ERROR(nrf24_prepare_rx(s_rf.config.left), TAG, "LEFT prepare failed");
    ESP_RETURN_ON_ERROR(nrf24_prepare_rx(s_rf.config.right), TAG, "RIGHT prepare failed");
    ESP_RETURN_ON_ERROR(nrf24_prepare_rx(s_rf.config.top), TAG, "TOP prepare failed");

    s_rf.initialized = true;
    return ESP_OK;
}

esp_err_t rf_scanner_start_task(void)
{
    ESP_RETURN_ON_FALSE(s_rf.initialized, ESP_ERR_INVALID_STATE, TAG, "scanner not initialized");
    BaseType_t ok = xTaskCreate(rf_scanner_task, "rf_scan", 6144, NULL, 4, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
