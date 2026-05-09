#pragma once

#include "esp_err.h"
#include "tft_display.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_manager_init(tft_display_t *display);
esp_err_t ui_manager_start_task(void);

#ifdef __cplusplus
}
#endif
