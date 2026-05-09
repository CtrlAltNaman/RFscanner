#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "nrf24.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool display_ready;
    bool nrf_ok[NRF24_POSITION_COUNT];
    bool menu_mode;
    int menu_index;
    char last_button[16];
} system_state_snapshot_t;

esp_err_t system_state_init(void);
void system_state_set_display_ready(bool ready);
void system_state_set_nrf_status(nrf24_position_t position, bool ok);
void system_state_set_last_button(const char *button_name);
void system_state_toggle_menu_mode(void);
bool system_state_is_menu_mode(void);
void system_state_move_menu(int delta, int item_count);
void system_state_get_snapshot(system_state_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
