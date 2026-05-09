#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_host_device_t host;
    gpio_num_t sck_pin;
    gpio_num_t mosi_pin;
    gpio_num_t miso_pin;
    spi_dma_chan_t dma_channel;
} spi_manager_config_t;

esp_err_t spi_manager_init(const spi_manager_config_t *config);
esp_err_t spi_manager_add_device(const spi_device_interface_config_t *device_config,
                                 spi_device_handle_t *out_handle);
void spi_manager_lock(TickType_t timeout_ticks);
void spi_manager_unlock(void);
spi_host_device_t spi_manager_get_host(void);

#ifdef __cplusplus
}
#endif
