#include "csi_mode.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "app_system.h"
#include "app_utils.h"

static const char *TAG = "csi_mode";

typedef struct {
    app_csi_state_t state;
    bool wifi_ready;
    bool wifi_started;
    bool csi_enabled;
    bool config_error_logged;
    volatile uint32_t packet_accum;
    volatile uint32_t amplitude_accum;
} csi_ctx_t;

static csi_ctx_t s_csi;

static void csi_rx_callback(void *ctx, wifi_csi_info_t *data)
{
    (void)ctx;
    uint32_t amplitude = 0U;
    const int len = data->len;

    for (int i = data->first_word_invalid ? 4 : 0; i + 1 < len; i += 2) {
        const int16_t i_part = data->buf[i];
        const int16_t q_part = data->buf[i + 1];
        amplitude += (uint32_t)(abs(i_part) + abs(q_part));
    }

    s_csi.packet_accum++;
    s_csi.amplitude_accum += amplitude;
}

static esp_err_t csi_enable(uint8_t channel)
{
#if !CONFIG_ESP_WIFI_CSI_ENABLED
    if (!s_csi.config_error_logged) {
        ESP_LOGE(TAG, "Wi-Fi CSI is disabled in sdkconfig. Enable CONFIG_ESP_WIFI_CSI_ENABLED.");
        s_csi.config_error_logged = true;
    }
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_csi.wifi_ready) {
        const esp_err_t netif_err = esp_netif_init();
        if ((netif_err != ESP_OK) && (netif_err != ESP_ERR_INVALID_STATE)) {
            return netif_err;
        }
        const esp_err_t loop_err = esp_event_loop_create_default();
        if ((loop_err != ESP_OK) && (loop_err != ESP_ERR_INVALID_STATE)) {
            return loop_err;
        }

        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif == NULL) {
            sta_netif = esp_netif_create_default_wifi_sta();
            if (sta_netif == NULL) {
                return ESP_FAIL;
            }
        }

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set_storage failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode failed");
        s_csi.wifi_ready = true;
    }

    if (!s_csi.wifi_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi_start failed");
        s_csi.wifi_started = true;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE), TAG, "set_channel failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_promiscuous(true), TAG, "promisc failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_csi_rx_cb(csi_rx_callback, NULL), TAG, "set_csi_rx_cb failed");

    wifi_csi_config_t config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
        .shift = 0,
        .dump_ack_en = false,
    };

    ESP_RETURN_ON_ERROR(esp_wifi_set_csi_config(&config), TAG, "set_csi_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_csi(true), TAG, "enable CSI failed");
    s_csi.csi_enabled = true;
    s_csi.config_error_logged = false;
    app_system_set_wifi_active(true);
    return ESP_OK;
#endif
}

static void csi_disable(void)
{
    if (!s_csi.wifi_ready) {
        return;
    }

    if (s_csi.csi_enabled) {
        (void)esp_wifi_set_csi(false);
        s_csi.csi_enabled = false;
    }
    (void)esp_wifi_set_promiscuous(false);
    if (s_csi.wifi_started) {
        (void)esp_wifi_stop();
        s_csi.wifi_started = false;
    }
    app_system_set_wifi_active(false);
}

static void csi_mode_task(void *arg)
{
    (void)arg;
    float level_ema = 0.0f;
    float baseline_ema = 0.0f;
    float delta_ema = 0.0f;

    while (true) {
        app_snapshot_t snapshot = {0};
        app_system_get_snapshot(&snapshot);
        const app_mode_t mode = snapshot.mode;
        const bool active = (mode == APP_MODE_CSI);
        const app_settings_t settings = app_system_get_settings();
        s_csi.state.paused = snapshot.csi.paused;
        s_csi.state.wifi_channel = snapshot.csi.wifi_channel;

        if (!active || s_csi.state.paused) {
            if (s_csi.csi_enabled) {
                csi_disable();
            }
            s_csi.state.active = active;
            app_system_set_csi_state(&s_csi.state);
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        s_csi.state.active = true;
        const esp_err_t enable_err = csi_enable(s_csi.state.wifi_channel);
        if (enable_err != ESP_OK) {
            s_csi.state.active = false;
            s_csi.state.packet_count = 0U;
            s_csi.state.amplitude = 0U;
            s_csi.state.activity = 0U;
            app_system_set_csi_state(&s_csi.state);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(100));

        const uint32_t packets = s_csi.packet_accum;
        const uint32_t amplitude = s_csi.amplitude_accum;
        s_csi.packet_accum = 0U;
        s_csi.amplitude_accum = 0U;

        const float sample_level = (packets > 0U) ? ((float)amplitude / (float)packets) : 0.0f;
        const float fast_alpha = 0.35f;
        const float baseline_alpha = 0.05f;
        const float delta_alpha = 0.25f;
        const float decay_level = 0.92f;
        const float decay_delta = 0.82f;

        if (packets > 0U) {
            level_ema = app_utils_ema(level_ema, sample_level, fast_alpha);
        } else {
            level_ema *= decay_level;
        }

        baseline_ema = app_utils_ema(baseline_ema, level_ema, baseline_alpha);

        const float instant_delta = fabsf(level_ema - baseline_ema);
        if (packets > 0U) {
            delta_ema = app_utils_ema(delta_ema, instant_delta, delta_alpha);
        } else {
            delta_ema *= decay_delta;
        }

        float activity_norm = sqrtf(delta_ema) * 18.0f;
        if (activity_norm > 255.0f) {
            activity_norm = 255.0f;
        }

        s_csi.state.packet_count += packets;
        s_csi.state.amplitude = (uint16_t)level_ema;
        s_csi.state.activity = (uint16_t)activity_norm;
        s_csi.state.waveform[s_csi.state.waveform_pos] = s_csi.state.activity;
        s_csi.state.waveform_pos = (uint8_t)((s_csi.state.waveform_pos + 1U) % APP_CSI_HISTORY_LEN);

        ESP_LOGI(TAG,
                 "csi ch=%u packets=%lu level=%u base=%u delta=%u activity=%u paused=%s",
                 s_csi.state.wifi_channel,
                 (unsigned long)packets,
                 s_csi.state.amplitude,
                 (unsigned)baseline_ema,
                 (unsigned)delta_ema,
                 s_csi.state.activity,
                 s_csi.state.paused ? "true" : "false");

        app_system_set_csi_state(&s_csi.state);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

esp_err_t csi_mode_init(void)
{
    memset(&s_csi, 0, sizeof(s_csi));
    s_csi.state.wifi_channel = 6U;
    return ESP_OK;
}

esp_err_t csi_mode_start_task(void)
{
    BaseType_t ok = xTaskCreate(csi_mode_task, "csi_task", 6144, NULL, 4, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
