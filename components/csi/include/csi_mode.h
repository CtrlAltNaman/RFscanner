#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t csi_mode_init(void);
esp_err_t csi_mode_start_task(void);

#ifdef __cplusplus
}
#endif
