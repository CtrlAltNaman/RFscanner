#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NRF24_POSITION_LEFT = 0,
    NRF24_POSITION_RIGHT,
    NRF24_POSITION_TOP,
    NRF24_POSITION_COUNT,
} nrf24_position_t;

typedef struct {
    const char *label;
    gpio_num_t ce_pin;
    gpio_num_t csn_pin;
} nrf24_config_t;

typedef struct {
    const char *label;
    gpio_num_t ce_pin;
    gpio_num_t csn_pin;
    spi_device_handle_t spi_handle;
    bool present;
} nrf24_device_t;

typedef struct {
    uint8_t status_nop;
    uint8_t setup_aw;
    uint8_t rf_ch;
    uint8_t config;
    bool bus_stuck_high;
    bool bus_stuck_low;
} nrf24_probe_report_t;

esp_err_t nrf24_init(nrf24_device_t *device, const nrf24_config_t *config);
bool nrf24_probe(nrf24_device_t *device);
bool nrf24_probe_with_report(nrf24_device_t *device, nrf24_probe_report_t *report);
esp_err_t nrf24_prepare_rx(nrf24_device_t *device);
esp_err_t nrf24_set_channel(nrf24_device_t *device, uint8_t channel);
esp_err_t nrf24_set_ce(nrf24_device_t *device, bool enable);
esp_err_t nrf24_read_rpd(nrf24_device_t *device, bool *detected);
esp_err_t nrf24_read_status(nrf24_device_t *device, uint8_t *status);
esp_err_t nrf24_clear_status(nrf24_device_t *device);
esp_err_t nrf24_flush_rx(nrf24_device_t *device);

#ifdef __cplusplus
}
#endif
