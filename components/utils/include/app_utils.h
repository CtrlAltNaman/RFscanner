#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float app_utils_ema(float previous, float input, float alpha);
uint8_t app_utils_clamp_u8(int value, int min_value, int max_value);
int app_utils_wrap_index(int value, int item_count);

#ifdef __cplusplus
}
#endif
