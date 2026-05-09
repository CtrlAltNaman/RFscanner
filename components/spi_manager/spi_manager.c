#include "spi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "spi_manager";

static spi_host_device_t s_spi_host = SPI2_HOST;
static SemaphoreHandle_t s_spi_mutex;
static bool s_initialized;

esp_err_t spi_manager_init(const spi_manager_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    if (s_initialized) {
        ESP_LOGW(TAG, "SPI manager already initialized");
        return ESP_OK;
    }

    spi_bus_config_t bus_config = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->sck_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 64 * 2,
    };

    s_spi_host = config->host;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(config->host, &bus_config, config->dma_channel),
                        TAG, "spi_bus_initialize failed");

    s_spi_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_spi_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create SPI mutex");

    s_initialized = true;
    ESP_LOGI(TAG, "SPI bus ready on host %d", config->host);
    return ESP_OK;
}

esp_err_t spi_manager_add_device(const spi_device_interface_config_t *device_config,
                                 spi_device_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "SPI manager not initialized");
    ESP_RETURN_ON_FALSE(device_config != NULL, ESP_ERR_INVALID_ARG, TAG, "device_config is null");
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "out_handle is null");

    return spi_bus_add_device(s_spi_host, device_config, out_handle);
}

void spi_manager_lock(TickType_t timeout_ticks)
{
    if (s_spi_mutex != NULL) {
        xSemaphoreTake(s_spi_mutex, timeout_ticks);
    }
}

void spi_manager_unlock(void)
{
    if (s_spi_mutex != NULL) {
        xSemaphoreGive(s_spi_mutex);
    }
}

spi_host_device_t spi_manager_get_host(void)
{
    return s_spi_host;
}
