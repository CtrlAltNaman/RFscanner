#pragma once

#include "esp_err.h"
#include "nrf24.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    nrf24_device_t *left;
    nrf24_device_t *right;
    nrf24_device_t *top;
} rf_scanner_config_t;

esp_err_t rf_scanner_init(const rf_scanner_config_t *config);
esp_err_t rf_scanner_start_task(void);

#ifdef __cplusplus
}
#endif
