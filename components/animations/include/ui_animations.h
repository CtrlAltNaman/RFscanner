#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t ui_animations_radar_x(uint32_t tick_ms, uint16_t width);
bool ui_animations_glitch_phase(uint32_t tick_ms, uint32_t period_ms);
uint8_t ui_animations_pulse(uint32_t tick_ms, uint32_t period_ms, uint8_t min_value, uint8_t max_value);

#ifdef __cplusplus
}
#endif
