#include "ui_animations.h"

uint16_t ui_animations_radar_x(uint32_t tick_ms, uint16_t width)
{
    if (width == 0U) {
        return 0U;
    }

    return (uint16_t)((tick_ms / 40U) % width);
}

bool ui_animations_glitch_phase(uint32_t tick_ms, uint32_t period_ms)
{
    if (period_ms == 0U) {
        period_ms = 1U;
    }

    return ((tick_ms / period_ms) % 2U) == 0U;
}

uint8_t ui_animations_pulse(uint32_t tick_ms, uint32_t period_ms, uint8_t min_value, uint8_t max_value)
{
    if (period_ms == 0U || max_value <= min_value) {
        return min_value;
    }

    const uint32_t phase = tick_ms % period_ms;
    const uint32_t half = period_ms / 2U;
    const uint32_t span = (uint32_t)(max_value - min_value);

    if (phase < half) {
        return (uint8_t)(min_value + ((span * phase) / (half == 0U ? 1U : half)));
    }

    return (uint8_t)(max_value - ((span * (phase - half)) / (half == 0U ? 1U : half)));
}
