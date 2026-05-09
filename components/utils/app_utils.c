#include "app_utils.h"

float app_utils_ema(float previous, float input, float alpha)
{
    if (alpha < 0.0f) {
        alpha = 0.0f;
    } else if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    return (previous * (1.0f - alpha)) + (input * alpha);
}

uint8_t app_utils_clamp_u8(int value, int min_value, int max_value)
{
    if (value < min_value) {
        value = min_value;
    }
    if (value > max_value) {
        value = max_value;
    }

    return (uint8_t)value;
}

int app_utils_wrap_index(int value, int item_count)
{
    if (item_count <= 0) {
        return 0;
    }

    while (value < 0) {
        value += item_count;
    }
    while (value >= item_count) {
        value -= item_count;
    }

    return value;
}
