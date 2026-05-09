#include "ui_screens.h"

#include <math.h>
#include <stdio.h>

#include "ui_widgets.h"

static void draw_pixel(tft_display_t *display, int x, int y, uint16_t color)
{
    tft_display_fill_rect(display, x, y, 1, 1, color);
}

static void draw_line(tft_display_t *display, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        draw_pixel(display, x0, y0, color);
        if ((x0 == x1) && (y0 == y1)) {
            break;
        }

        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_ring(tft_display_t *display, int cx, int cy, int radius, uint16_t color)
{
    for (int deg = 0; deg < 360; deg += 6) {
        const float rad = (float)deg * 0.0174532925f;
        const int x = cx + (int)((float)radius * cosf(rad));
        const int y = cy + (int)((float)radius * sinf(rad));
        draw_pixel(display, x, y, color);
    }
}

static void render_boot(tft_display_t *display, const app_snapshot_t *snapshot, bool full_redraw)
{
    static const char *LOGS[] = {
        "INITIALIZING RF SURVEILLANCE SYSTEM",
        "SPI BUS ............ OK",
        "DISPLAY ............ OK",
        "NRF LEFT ........... OK",
        "NRF RIGHT .......... OK",
        "NRF TOP ............ OK",
        "BUTTONS ............ OK",
    };

    if (full_redraw) {
        tft_display_clear(display, TFT_COLOR_BLACK);
        ui_widgets_draw_header(display, "BOOT SEQUENCE");
    }

    const int visible_lines = 1 + (snapshot->uptime_ms / 500U);
    for (int i = 0; i < visible_lines && i < 7; ++i) {
        tft_display_draw_text(display, 4, 18 + (i * 12), LOGS[i], ui_widgets_theme_accent(), TFT_COLOR_BLACK, 1);
    }

    tft_display_fill_rect(display, 0, 106, 160, 18, TFT_COLOR_BLACK);
    tft_display_draw_text(display, 4, 106, "PRESS TO SKIP", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    ui_widgets_draw_bar(display, 4, 118, 152, 8, (uint8_t)((snapshot->uptime_ms > 4200U ? 4200U : snapshot->uptime_ms) * 100U / 4200U), ui_widgets_theme_accent());
}

static void render_menu(tft_display_t *display, const app_snapshot_t *snapshot, uint32_t tick_ms)
{
    static const char *ITEMS[] = {
        "RF SCAN MODE",
        "CSI MODE",
        "SIGNAL ANALYZER",
        "SYSTEM STATUS",
        "SETTINGS",
        "ABOUT SYSTEM",
    };

    tft_display_clear(display, TFT_COLOR_BLACK);
    ui_widgets_draw_header(display, "MAIN MENU");
    (void)tick_ms;

    for (int i = 0; i < 6; ++i) {
        const bool selected = (snapshot->menu_index == i);
        const uint16_t fg = selected ? TFT_COLOR_BLACK : ui_widgets_theme_accent();
        const uint16_t bg = selected ? ui_widgets_theme_accent() : TFT_COLOR_BLACK;

        if (selected) {
            tft_display_fill_rect(display, 6, 18 + (i * 16), 148, 12, bg);
        }

        tft_display_draw_text(display, 10, 20 + (i * 16), ITEMS[i], fg, bg, 1);
    }

    tft_display_draw_text(display, 4, 116, "UP/DN MOVE CONFIRM OPEN", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
}

static void render_rf_scan(tft_display_t *display, const app_snapshot_t *snapshot, uint32_t tick_ms, bool full_redraw)
{
    char buffer[24];

    if (full_redraw) {
        tft_display_clear(display, TFT_COLOR_BLACK);
        ui_widgets_draw_header(display, "RF SCAN MODE");
    }

    if (snapshot->rf.view_mode == APP_RF_VIEW_RADAR) {
        const int cx = 80;
        const int cy = 76;
        const int max_radius = 40;
        const int left_x = cx - 28;
        const int right_x = cx + 28;
        const int top_y = cy - 28;
        const int edge_x = (int)snapshot->rf.right_strength - (int)snapshot->rf.left_strength;
        const int edge_y = (((int)snapshot->rf.left_strength + (int)snapshot->rf.right_strength) / 2) - (int)snapshot->rf.top_strength;
        const float strength = (float)snapshot->rf.strongest_intensity / 100.0f;
        const float radius_bias = 1.0f - (strength > 1.0f ? 1.0f : strength);
        int target_x = cx + (edge_x * 3) / 2;
        int target_y = cy + (edge_y * 3) / 2 + (int)(radius_bias * 12.0f);

        if (target_x < (cx - max_radius)) {
            target_x = cx - max_radius;
        } else if (target_x > (cx + max_radius)) {
            target_x = cx + max_radius;
        }
        if (target_y < (cy - max_radius)) {
            target_y = cy - max_radius;
        } else if (target_y > (cy + max_radius)) {
            target_y = cy + max_radius;
        }

        ui_widgets_draw_status_line(display, 16, "VIEW", "RADAR", ui_widgets_theme_alt());
        ui_widgets_draw_status_line(display, 26, "MODE", snapshot->rf.scan_mode == APP_RF_MODE_TRACK ? "TRACK" : "SCAN", ui_widgets_theme_alt());
        ui_widgets_draw_status_line(display, 36, "DIR", ui_widgets_direction_text(snapshot->rf.direction), ui_widgets_theme_accent());
        snprintf(buffer, sizeof(buffer), "CH %03u", snapshot->rf.strongest_channel);
        ui_widgets_draw_status_line(display, 46, "PEAK", buffer, ui_widgets_theme_accent());

        tft_display_fill_rect(display, 0, 56, 160, 52, TFT_COLOR_BLACK);
        draw_ring(display, cx, cy, 14, ui_widgets_theme_dim());
        draw_ring(display, cx, cy, 28, ui_widgets_theme_dim());
        draw_ring(display, cx, cy, 40, ui_widgets_theme_dim());
        draw_line(display, cx - 40, cy, cx + 40, cy, ui_widgets_theme_dim());
        draw_line(display, cx, cy - 40, cx, cy + 40, ui_widgets_theme_dim());
        draw_line(display, left_x, cy + 18, cx, top_y, ui_widgets_theme_dim());
        draw_line(display, cx, top_y, right_x, cy + 18, ui_widgets_theme_dim());
        draw_line(display, left_x, cy + 18, right_x, cy + 18, ui_widgets_theme_dim());
        tft_display_fill_rect(display, left_x - 2, cy + 16, 5, 5, ui_widgets_theme_accent());
        tft_display_fill_rect(display, right_x - 2, cy + 16, 5, 5, ui_widgets_theme_alt());
        tft_display_fill_rect(display, cx - 2, top_y - 2, 5, 5, TFT_COLOR_RED);
        draw_line(display, left_x, cy + 18, target_x, target_y, ui_widgets_theme_accent());
        draw_line(display, right_x, cy + 18, target_x, target_y, ui_widgets_theme_alt());
        draw_line(display, cx, top_y, target_x, target_y, TFT_COLOR_RED);
        tft_display_fill_rect(display, target_x - 2, target_y - 2, 5, 5, TFT_COLOR_YELLOW);

        tft_display_fill_rect(display, 0, 108, 160, 16, TFT_COLOR_BLACK);
        snprintf(buffer, sizeof(buffer), "L%02u R%02u T%02u", snapshot->rf.left_strength, snapshot->rf.right_strength, snapshot->rf.top_strength);
        tft_display_draw_text(display, 4, 108, buffer, ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
        snprintf(buffer, sizeof(buffer), "S%03u %lus", snapshot->rf.strongest_intensity, (unsigned long)(tick_ms / 1000U));
        tft_display_draw_text(display, 92, 108, buffer, ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
        tft_display_fill_rect(display, 0, 116, 160, 8, TFT_COLOR_BLACK);
        tft_display_draw_text(display, 4, 116, snapshot->rf.scan_mode == APP_RF_MODE_TRACK ? "UP BARS CONFIRM SCAN" : "UP BARS CONFIRM TRACK", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    } else {
        snprintf(buffer, sizeof(buffer), "CH %03u", snapshot->rf.current_channel);
        ui_widgets_draw_status_line(display, 16, "ACTIVE CH", buffer, ui_widgets_theme_accent());
        snprintf(buffer, sizeof(buffer), "CH %03u", snapshot->rf.strongest_channel);
        ui_widgets_draw_status_line(display, 26, "PEAK CH", buffer, ui_widgets_theme_accent());
        ui_widgets_draw_status_line(display, 36, "MODE", snapshot->rf.scan_mode == APP_RF_MODE_TRACK ? "TRACK" : "SCAN", ui_widgets_theme_alt());
        ui_widgets_draw_status_line(display, 46, "DIR", ui_widgets_direction_text(snapshot->rf.direction), ui_widgets_theme_alt());
        snprintf(buffer, sizeof(buffer), "%uMS", snapshot->settings.scan_dwell_ms);
        ui_widgets_draw_status_line(display, 56, "DWELL", buffer, ui_widgets_theme_dim());

        tft_display_fill_rect(display, 0, 68, 160, 28, TFT_COLOR_BLACK);
        tft_display_draw_text(display, 4, 68, "LEFT", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
        ui_widgets_draw_bar(display, 36, 68, 120, 8, snapshot->rf.left_strength, ui_widgets_theme_accent());
        tft_display_draw_text(display, 4, 80, "RIGHT", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
        ui_widgets_draw_bar(display, 36, 80, 120, 8, snapshot->rf.right_strength, ui_widgets_theme_alt());
        tft_display_draw_text(display, 4, 92, "TOP", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
        ui_widgets_draw_bar(display, 36, 92, 120, 8, snapshot->rf.top_strength, TFT_COLOR_RED);

        ui_widgets_draw_history_graph(display, 4, 106, 152, 10, snapshot->rf.history, APP_RF_HISTORY_LEN, ui_widgets_theme_accent());
        tft_display_fill_rect(display, 0, 116, 160, 8, TFT_COLOR_BLACK);
        tft_display_draw_text(display, 4, 116, snapshot->rf.scan_mode == APP_RF_MODE_TRACK ? "DN RADAR CONFIRM SCAN" : "DN RADAR CONFIRM TRACK", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    }
}

static void render_csi(tft_display_t *display, const app_snapshot_t *snapshot, bool full_redraw)
{
    char buffer[24];

    if (full_redraw) {
        tft_display_clear(display, TFT_COLOR_BLACK);
        ui_widgets_draw_header(display, "CSI MODE");
    }

    snprintf(buffer, sizeof(buffer), "CH %02u", snapshot->csi.wifi_channel);
    ui_widgets_draw_status_line(display, 16, "WIFI CH", buffer, ui_widgets_theme_alt());
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)snapshot->csi.packet_count);
    ui_widgets_draw_status_line(display, 26, "PKTS", buffer, ui_widgets_theme_accent());
    snprintf(buffer, sizeof(buffer), "%u", snapshot->csi.amplitude);
    ui_widgets_draw_status_line(display, 36, "AMPL", buffer, ui_widgets_theme_accent());
    snprintf(buffer, sizeof(buffer), "%u", snapshot->csi.activity);
    ui_widgets_draw_status_line(display, 46, "MOTION", buffer, TFT_COLOR_RED);

    ui_widgets_draw_waveform(display, 4, 58, 152, 38, snapshot->csi.waveform, APP_CSI_HISTORY_LEN, ui_widgets_theme_accent());
    tft_display_fill_rect(display, 0, 102, 160, 22, TFT_COLOR_BLACK);
    tft_display_draw_text(display, 4, 102, snapshot->csi.paused ? "CSI LINK: PAUSED" : "CSI LINK: ACTIVE", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 110, snapshot->csi.paused ? "CONFIRM RESUME" : "CONFIRM PAUSE", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 116, "UP/DN CH  HOLD CONF BACK", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
}

static void render_analyzer(tft_display_t *display, const app_snapshot_t *snapshot)
{
    char buffer[16];

    tft_display_clear(display, TFT_COLOR_BLACK);
    ui_widgets_draw_header(display, "SIGNAL ANALYZER");

    if (snapshot->analyzer_page == 0) {
        ui_widgets_draw_status_line(display, 16, "RF DIR", ui_widgets_direction_text(snapshot->rf.direction), ui_widgets_theme_alt());
        ui_widgets_draw_status_line(display, 26, "RF PEAK", "LIVE", ui_widgets_theme_accent());
        ui_widgets_draw_bar(display, 4, 42, 152, 10, snapshot->rf.strongest_intensity, ui_widgets_theme_accent());
        ui_widgets_draw_status_line(display, 58, "CSI ACT", "LIVE", TFT_COLOR_RED);
        ui_widgets_draw_bar(display, 4, 74, 152, 10, (uint8_t)((snapshot->csi.activity > 100U) ? 100U : snapshot->csi.activity), TFT_COLOR_RED);
    } else if (snapshot->analyzer_page == 1) {
        ui_widgets_draw_history_graph(display, 4, 18, 152, 40, snapshot->rf.history, APP_RF_HISTORY_LEN, ui_widgets_theme_alt());
        ui_widgets_draw_waveform(display, 4, 64, 152, 40, snapshot->csi.waveform, APP_CSI_HISTORY_LEN, ui_widgets_theme_accent());
    } else {
        tft_display_draw_text(display, 4, 18, "OCCUPANCY HEATMAP", ui_widgets_theme_accent(), TFT_COLOR_BLACK, 1);
        for (int i = 0; i < 30; ++i) {
            const uint8_t level = snapshot->rf.occupancy[(i * 4) % 126];
            tft_display_fill_rect(display, 4 + (i * 5), 96 - (level / 3), 3, level / 3, TFT_COLOR_RED);
        }
    }

    tft_display_draw_text(display, 4, 106, "PAGE", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    snprintf(buffer, sizeof(buffer), "%u/3", (unsigned)(snapshot->analyzer_page + 1));
    tft_display_draw_text(display, 34, 106, buffer, ui_widgets_theme_accent(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 116, "UP/DN PAGE CONFIRM NEXT", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
}

static void render_status(tft_display_t *display, const app_snapshot_t *snapshot)
{
    char buffer[24];

    tft_display_clear(display, TFT_COLOR_BLACK);
    ui_widgets_draw_header(display, "SYSTEM STATUS");

    ui_widgets_draw_status_line(display, 16, "MODE", app_system_mode_name(snapshot->mode), ui_widgets_theme_alt());
    ui_widgets_draw_status_line(display, 26, "SPI", snapshot->spi_ok ? "OK" : "FAIL", snapshot->spi_ok ? ui_widgets_theme_accent() : TFT_COLOR_RED);
    ui_widgets_draw_status_line(display, 36, "LEFT NRF", snapshot->nrf_ok[0] ? "OK" : "FAIL", snapshot->nrf_ok[0] ? ui_widgets_theme_accent() : TFT_COLOR_RED);
    ui_widgets_draw_status_line(display, 46, "RIGHT NRF", snapshot->nrf_ok[1] ? "OK" : "FAIL", snapshot->nrf_ok[1] ? ui_widgets_theme_accent() : TFT_COLOR_RED);
    ui_widgets_draw_status_line(display, 56, "TOP NRF", snapshot->nrf_ok[2] ? "OK" : "FAIL", snapshot->nrf_ok[2] ? ui_widgets_theme_accent() : TFT_COLOR_RED);
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)snapshot->free_heap);
    ui_widgets_draw_status_line(display, 66, "HEAP", buffer, ui_widgets_theme_alt());
    snprintf(buffer, sizeof(buffer), "%u", snapshot->fps);
    ui_widgets_draw_status_line(display, 76, "FPS", buffer, ui_widgets_theme_alt());
    snprintf(buffer, sizeof(buffer), "%luS", (unsigned long)(snapshot->uptime_ms / 1000U));
    ui_widgets_draw_status_line(display, 86, "UPTIME", buffer, ui_widgets_theme_alt());
    ui_widgets_draw_status_line(display, 96, "WIFI", snapshot->wifi_active ? "ACTIVE" : "IDLE", snapshot->wifi_active ? ui_widgets_theme_accent() : ui_widgets_theme_dim());
    ui_widgets_draw_status_line(display, 106, "BATT", "--.-V", ui_widgets_theme_dim());
    tft_display_draw_text(display, 4, 116, "HOLD CONFIRM BACK", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
}

static void render_settings(tft_display_t *display, const app_snapshot_t *snapshot)
{
    char buffer[20];
    const char *labels[6] = { "BRIGHT", "ANIM", "SCAN", "SMOOTH", "THEME", "SOUND" };

    tft_display_clear(display, TFT_COLOR_BLACK);
    ui_widgets_draw_header(display, "SETTINGS");

    for (int i = 0; i < 6; ++i) {
        const bool selected = (snapshot->settings_index == i);
        const uint16_t fg = selected ? TFT_COLOR_BLACK : ui_widgets_theme_accent();
        const uint16_t bg = selected ? ui_widgets_theme_accent() : TFT_COLOR_BLACK;
        const int row_y = 16 + (i * 14);

        if (selected) {
            tft_display_fill_rect(display, 4, row_y, 152, 12, bg);
        }

        switch (i) {
        case 0:
            snprintf(buffer, sizeof(buffer), "%u%%", snapshot->settings.brightness_pct);
            break;
        case 1:
            snprintf(buffer, sizeof(buffer), "%u%%", snapshot->settings.animation_speed_pct);
            break;
        case 2:
            snprintf(buffer, sizeof(buffer), "%uMS", snapshot->settings.scan_dwell_ms);
            break;
        case 3:
            snprintf(buffer, sizeof(buffer), "%u%%", snapshot->settings.smoothing_pct);
            break;
        case 4:
            snprintf(buffer, sizeof(buffer), "%s", snapshot->settings.color_theme == 0 ? "GREEN" :
                                                 snapshot->settings.color_theme == 1 ? "CYAN" : "RED");
            break;
        case 5:
            snprintf(buffer, sizeof(buffer), "%s", snapshot->settings.sound_enabled ? "ON" : "OFF");
            break;
        default:
            buffer[0] = '\0';
            break;
        }

        tft_display_draw_text(display, 8, row_y + 2, labels[i], fg, bg, 1);
        tft_display_draw_text(display, 92, row_y + 2, buffer, fg, bg, 1);
    }

    tft_display_draw_text(display, 4, 108, "UP/DN ITEM", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 118, "CONFIRM EDIT  HOLD BACK", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
}

static void render_about(tft_display_t *display)
{
    tft_display_clear(display, TFT_COLOR_BLACK);
    ui_widgets_draw_header(display, "ABOUT SYSTEM");
    tft_display_draw_text(display, 4, 18, "RF SURVEILLANCE UNIT", ui_widgets_theme_accent(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 34, "ESP32-S3 / 3X NRF24L01", ui_widgets_theme_alt(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 50, "TACTICAL RF + CSI CORE", ui_widgets_theme_accent(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 66, "BUILD: CYBER CONSOLE", ui_widgets_theme_accent(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 82, "VERSION: 1.0 PROTOTYPE", ui_widgets_theme_alt(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 98, "TEAM: EMBEDDED LAB", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
    tft_display_draw_text(display, 4, 116, "HOLD CONFIRM BACK", ui_widgets_theme_dim(), TFT_COLOR_BLACK, 1);
}

void ui_screens_render(tft_display_t *display, const app_snapshot_t *snapshot, uint32_t tick_ms, bool full_redraw)
{
    switch (snapshot->mode) {
    case APP_MODE_BOOT:
        render_boot(display, snapshot, full_redraw);
        break;
    case APP_MODE_MAIN_MENU:
        render_menu(display, snapshot, tick_ms);
        break;
    case APP_MODE_RF_SCAN:
        render_rf_scan(display, snapshot, tick_ms, full_redraw);
        break;
    case APP_MODE_CSI:
        render_csi(display, snapshot, full_redraw);
        break;
    case APP_MODE_ANALYZER:
        render_analyzer(display, snapshot);
        break;
    case APP_MODE_STATUS:
        render_status(display, snapshot);
        break;
    case APP_MODE_SETTINGS:
        render_settings(display, snapshot);
        break;
    case APP_MODE_ABOUT:
        render_about(display);
        break;
    default:
        break;
    }
}
