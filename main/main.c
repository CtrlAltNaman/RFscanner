#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_system.h"
#include "buttons.h"
#include "csi_mode.h"
#include "device_board.h"
#include "nrf24.h"
#include "rf_scanner.h"
#include "spi_manager.h"
#include "tft_display.h"
#include "ui_manager.h"

static const char *TAG = "app_main";

static tft_display_t g_display;
static nrf24_device_t g_nrf_left;
static nrf24_device_t g_nrf_right;
static nrf24_device_t g_nrf_top;

static void prepare_all_nrf_control_pins(void)
{
    const uint64_t ce_mask = (1ULL << BOARD_NRF_LEFT_CE_PIN) |
                             (1ULL << BOARD_NRF_RIGHT_CE_PIN) |
                             (1ULL << BOARD_NRF_TOP_CE_PIN);
    const uint64_t csn_mask = (1ULL << BOARD_NRF_LEFT_CSN_PIN) |
                              (1ULL << BOARD_NRF_RIGHT_CSN_PIN) |
                              (1ULL << BOARD_NRF_TOP_CSN_PIN);

    gpio_config_t ce_config = {
        .pin_bit_mask = ce_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&ce_config));

    gpio_config_t csn_config = {
        .pin_bit_mask = csn_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&csn_config));

    gpio_set_level(BOARD_NRF_LEFT_CE_PIN, 0);
    gpio_set_level(BOARD_NRF_RIGHT_CE_PIN, 0);
    gpio_set_level(BOARD_NRF_TOP_CE_PIN, 0);
    gpio_set_level(BOARD_NRF_LEFT_CSN_PIN, 1);
    gpio_set_level(BOARD_NRF_RIGHT_CSN_PIN, 1);
    gpio_set_level(BOARD_NRF_TOP_CSN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void on_button_pressed(button_id_t button, button_event_t event, void *user_ctx)
{
    (void)user_ctx;

    const char *name = buttons_get_name(button);
    const char *event_name = buttons_get_event_name(event);
    ESP_LOGI(TAG, "Button event: %s -> %s", name, event_name);
    app_system_set_last_button(name);
    app_system_handle_button(button, event);
}

static esp_err_t init_nrf_device(nrf24_device_t *device,
                                 const char *label,
                                 nrf24_position_t position,
                                 gpio_num_t ce_pin,
                                 gpio_num_t csn_pin)
{
    nrf24_config_t config = {
        .label = label,
        .ce_pin = ce_pin,
        .csn_pin = csn_pin,
    };

    ESP_RETURN_ON_ERROR(nrf24_init(device, &config), TAG, "Failed to init %s NRF", label);

    nrf24_probe_report_t report = {0};
    bool ok = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        ok = nrf24_probe_with_report(device, &report);
        if (ok) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    app_system_set_nrf_status(position, ok);
    if (!ok) {
        ESP_LOGW(TAG, "%s NRF failed probe", label);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting final RF surveillance firmware");

    esp_err_t nvs_err = nvs_flash_init();
    if ((nvs_err == ESP_ERR_NVS_NO_FREE_PAGES) || (nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_ERROR_CHECK(app_system_init());

    spi_manager_config_t spi_config = {
        .host = SPI2_HOST,
        .sck_pin = BOARD_SPI_SCK_PIN,
        .mosi_pin = BOARD_SPI_MOSI_PIN,
        .miso_pin = BOARD_SPI_MISO_PIN,
        .dma_channel = SPI_DMA_CH_AUTO,
    };
    ESP_ERROR_CHECK(spi_manager_init(&spi_config));
    app_system_set_spi_ok(true);

    tft_display_config_t display_config = {
        .cs_pin = BOARD_TFT_CS_PIN,
        .dc_pin = BOARD_TFT_DC_PIN,
        .reset_pin = BOARD_TFT_RESET_PIN,
        .backlight_pin = BOARD_TFT_BACKLIGHT_PIN,
        .width = BOARD_DISPLAY_WIDTH,
        .height = BOARD_DISPLAY_HEIGHT,
        .rotation = TFT_ROTATION_90,
    };
    ESP_ERROR_CHECK(tft_display_init(&g_display, &display_config));
    app_system_set_display_ok(true);

    prepare_all_nrf_control_pins();
    (void)init_nrf_device(&g_nrf_left, "LEFT", NRF24_POSITION_LEFT, BOARD_NRF_LEFT_CE_PIN, BOARD_NRF_LEFT_CSN_PIN);
    (void)init_nrf_device(&g_nrf_right, "RIGHT", NRF24_POSITION_RIGHT, BOARD_NRF_RIGHT_CE_PIN, BOARD_NRF_RIGHT_CSN_PIN);
    (void)init_nrf_device(&g_nrf_top, "TOP", NRF24_POSITION_TOP, BOARD_NRF_TOP_CE_PIN, BOARD_NRF_TOP_CSN_PIN);

    buttons_config_t buttons_config = {
        .up_pin = BOARD_BUTTON_UP_PIN,
        .down_pin = BOARD_BUTTON_DOWN_PIN,
        .confirm_pin = BOARD_BUTTON_CONFIRM_PIN,
        .poll_period_ms = 20,
        .debounce_count = 3,
        .callback = on_button_pressed,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(buttons_init(&buttons_config));
    app_system_set_buttons_ok(true);

    rf_scanner_config_t rf_config = {
        .left = &g_nrf_left,
        .right = &g_nrf_right,
        .top = &g_nrf_top,
    };
    ESP_ERROR_CHECK(rf_scanner_init(&rf_config));
    ESP_ERROR_CHECK(csi_mode_init());
    ESP_ERROR_CHECK(ui_manager_init(&g_display));
    ESP_ERROR_CHECK(app_system_start_monitor_task());
    ESP_ERROR_CHECK(rf_scanner_start_task());
    ESP_ERROR_CHECK(csi_mode_start_task());
    ESP_ERROR_CHECK(ui_manager_start_task());

    ESP_LOGI(TAG, "Final system tasks started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
