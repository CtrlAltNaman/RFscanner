#include "nrf24.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "spi_manager.h"

#define NRF24_CMD_R_REGISTER 0x00
#define NRF24_CMD_W_REGISTER 0x20
#define NRF24_CMD_FLUSH_RX   0xE2
#define NRF24_CMD_NOP        0xFF
#define NRF24_REG_CONFIG     0x00
#define NRF24_REG_EN_AA      0x01
#define NRF24_REG_EN_RXADDR  0x02
#define NRF24_REG_SETUP_RETR 0x04
#define NRF24_REG_RF_CH 0x05
#define NRF24_REG_RF_SETUP   0x06
#define NRF24_REG_STATUS 0x07
#define NRF24_REG_RPD        0x09
#define NRF24_REG_SETUP_AW 0x03

#define NRF24_STATUS_RX_DR   0x40
#define NRF24_STATUS_TX_DS   0x20
#define NRF24_STATUS_MAX_RT  0x10

static const char *TAG = "nrf24";

static esp_err_t nrf24_transfer(nrf24_device_t *device,
                                const uint8_t *tx_data,
                                uint8_t *rx_data,
                                size_t size)
{
    spi_transaction_t transaction = {
        .length = size * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    spi_manager_lock(portMAX_DELAY);
    const esp_err_t err = spi_device_polling_transmit(device->spi_handle, &transaction);
    spi_manager_unlock();
    return err;
}

static esp_err_t nrf24_read_register(nrf24_device_t *device, uint8_t reg, uint8_t *value)
{
    uint8_t tx_data[2] = { NRF24_CMD_R_REGISTER | (reg & 0x1F), 0xFF };
    uint8_t rx_data[2] = {0};

    ESP_RETURN_ON_ERROR(nrf24_transfer(device, tx_data, rx_data, sizeof(tx_data)),
                        TAG, "Read reg 0x%02X failed", reg);
    *value = rx_data[1];
    return ESP_OK;
}

static esp_err_t nrf24_write_register(nrf24_device_t *device, uint8_t reg, uint8_t value)
{
    uint8_t tx_data[2] = { NRF24_CMD_W_REGISTER | (reg & 0x1F), value };
    return nrf24_transfer(device, tx_data, NULL, sizeof(tx_data));
}

static esp_err_t nrf24_write_command(nrf24_device_t *device, uint8_t command)
{
    return nrf24_transfer(device, &command, NULL, sizeof(command));
}

static esp_err_t nrf24_read_status_nop(nrf24_device_t *device, uint8_t *status)
{
    uint8_t tx_data[1] = { NRF24_CMD_NOP };
    uint8_t rx_data[1] = {0};

    ESP_RETURN_ON_ERROR(nrf24_transfer(device, tx_data, rx_data, sizeof(tx_data)),
                        TAG, "NOP status read failed");
    *status = rx_data[0];
    return ESP_OK;
}

esp_err_t nrf24_init(nrf24_device_t *device, const nrf24_config_t *config)
{
    ESP_RETURN_ON_FALSE(device != NULL, ESP_ERR_INVALID_ARG, TAG, "device is null");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    memset(device, 0, sizeof(*device));
    device->label = config->label;
    device->ce_pin = config->ce_pin;
    device->csn_pin = config->csn_pin;

    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << config->ce_pin) | (1ULL << config->csn_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "Failed to configure CE/CSN");
    ESP_RETURN_ON_ERROR(gpio_set_level(config->ce_pin, 0), TAG, "Failed to drive CE low");
    ESP_RETURN_ON_ERROR(gpio_set_level(config->csn_pin, 1), TAG, "Failed to drive CSN high");
    vTaskDelay(pdMS_TO_TICKS(2));

    spi_device_interface_config_t spi_config = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = config->csn_pin,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
    };

    ESP_RETURN_ON_ERROR(spi_manager_add_device(&spi_config, &device->spi_handle),
                        TAG, "Failed to add %s NRF to SPI bus", config->label);

    ESP_LOGI(TAG, "%s NRF device attached to shared SPI bus", config->label);
    return ESP_OK;
}

bool nrf24_probe_with_report(nrf24_device_t *device, nrf24_probe_report_t *report)
{
    ESP_RETURN_ON_FALSE(device != NULL, false, TAG, "device is null");
    ESP_RETURN_ON_FALSE(report != NULL, false, TAG, "report is null");

    memset(report, 0, sizeof(*report));
    gpio_set_level(device->ce_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    if (nrf24_read_status_nop(device, &report->status_nop) != ESP_OK) {
        return false;
    }
    if (nrf24_read_register(device, NRF24_REG_SETUP_AW, &report->setup_aw) != ESP_OK) {
        return false;
    }
    if (nrf24_read_register(device, NRF24_REG_CONFIG, &report->config) != ESP_OK) {
        return false;
    }
    if (nrf24_read_register(device, NRF24_REG_RF_CH, &report->rf_ch) != ESP_OK) {
        return false;
    }

    report->bus_stuck_high =
        (report->status_nop == 0xFF) && (report->setup_aw == 0xFF) &&
        (report->config == 0xFF) && (report->rf_ch == 0xFF);
    report->bus_stuck_low =
        (report->status_nop == 0x00) && (report->setup_aw == 0x00) &&
        (report->config == 0x00) && (report->rf_ch == 0x00);

    if (report->bus_stuck_high || report->bus_stuck_low) {
        ESP_LOGW(TAG,
                 "%s NRF bus looks stuck: nop=0x%02X setup_aw=0x%02X config=0x%02X rf_ch=0x%02X",
                 device->label,
                 report->status_nop,
                 report->setup_aw,
                 report->config,
                 report->rf_ch);
        return false;
    }

    uint8_t original_rf_ch = report->rf_ch;
    const uint8_t probe_channel = 76;
    if (nrf24_write_register(device, NRF24_REG_RF_CH, probe_channel) != ESP_OK) {
        return false;
    }

    uint8_t readback_rf_ch = 0;
    if (nrf24_read_register(device, NRF24_REG_RF_CH, &readback_rf_ch) != ESP_OK) {
        return false;
    }

    (void)nrf24_write_register(device, NRF24_REG_RF_CH, original_rf_ch);

    device->present = (readback_rf_ch == probe_channel);
    ESP_LOGI(TAG,
             "%s NRF probe result: present=%s nop=0x%02X setup_aw=0x%02X config=0x%02X rf_ch=0x%02X",
             device->label,
             device->present ? "true" : "false",
             report->status_nop,
             report->setup_aw,
             report->config,
             readback_rf_ch);
    return device->present;
}

bool nrf24_probe(nrf24_device_t *device)
{
    nrf24_probe_report_t report = {0};
    return nrf24_probe_with_report(device, &report);
}

esp_err_t nrf24_set_ce(nrf24_device_t *device, bool enable)
{
    ESP_RETURN_ON_FALSE(device != NULL, ESP_ERR_INVALID_ARG, TAG, "device is null");
    return gpio_set_level(device->ce_pin, enable ? 1 : 0);
}

esp_err_t nrf24_set_channel(nrf24_device_t *device, uint8_t channel)
{
    ESP_RETURN_ON_FALSE(channel <= 125, ESP_ERR_INVALID_ARG, TAG, "channel out of range");
    return nrf24_write_register(device, NRF24_REG_RF_CH, channel);
}

esp_err_t nrf24_read_rpd(nrf24_device_t *device, bool *detected)
{
    uint8_t rpd = 0;
    ESP_RETURN_ON_FALSE(detected != NULL, ESP_ERR_INVALID_ARG, TAG, "detected is null");
    ESP_RETURN_ON_ERROR(nrf24_read_register(device, NRF24_REG_RPD, &rpd), TAG, "Failed to read RPD");
    *detected = (rpd & 0x01U) != 0U;
    return ESP_OK;
}

esp_err_t nrf24_read_status(nrf24_device_t *device, uint8_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is null");
    return nrf24_read_status_nop(device, status);
}

esp_err_t nrf24_clear_status(nrf24_device_t *device)
{
    return nrf24_write_register(device, NRF24_REG_STATUS,
                                NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT);
}

esp_err_t nrf24_flush_rx(nrf24_device_t *device)
{
    return nrf24_write_command(device, NRF24_CMD_FLUSH_RX);
}

esp_err_t nrf24_prepare_rx(nrf24_device_t *device)
{
    /*
     * Minimal receive setup for energy/RPD style scanning. Auto-ack and retries
     * are disabled because we are not participating in a link during scans.
     */
    ESP_RETURN_ON_ERROR(nrf24_set_ce(device, false), TAG, "Failed to drop CE");
    ESP_RETURN_ON_ERROR(nrf24_write_register(device, NRF24_REG_EN_AA, 0x00), TAG, "EN_AA failed");
    ESP_RETURN_ON_ERROR(nrf24_write_register(device, NRF24_REG_EN_RXADDR, 0x01), TAG, "EN_RXADDR failed");
    ESP_RETURN_ON_ERROR(nrf24_write_register(device, NRF24_REG_SETUP_RETR, 0x00), TAG, "SETUP_RETR failed");
    ESP_RETURN_ON_ERROR(nrf24_write_register(device, NRF24_REG_RF_SETUP, 0x0F), TAG, "RF_SETUP failed");
    ESP_RETURN_ON_ERROR(nrf24_write_register(device, NRF24_REG_CONFIG, 0x0F), TAG, "CONFIG failed");
    ESP_RETURN_ON_ERROR(nrf24_clear_status(device), TAG, "STATUS clear failed");
    ESP_RETURN_ON_ERROR(nrf24_flush_rx(device), TAG, "FLUSH_RX failed");
    ESP_RETURN_ON_ERROR(nrf24_set_ce(device, true), TAG, "Failed to raise CE");
    vTaskDelay(pdMS_TO_TICKS(2));
    return ESP_OK;
}
