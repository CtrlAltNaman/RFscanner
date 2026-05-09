#pragma once

#include <stdint.h>

#include "app_system.h"
#include "tft_display.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_screens_render(tft_display_t *display, const app_snapshot_t *snapshot, uint32_t tick_ms, bool full_redraw);

#ifdef __cplusplus
}
#endif
