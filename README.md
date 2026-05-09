# RFscanner

`RFscanner` is an ESP32-S3 firmware project for a handheld RF surveillance prototype. The active firmware combines three nRF24L01 radio front ends, a small SPI TFT display, three hardware buttons, and ESP32 Wi-Fi CSI capture into a menu-driven embedded UI.

The codebase is organized as a modern ESP-IDF application under `main/` and `components/`. There is also a `PJ/` folder containing older Arduino-era reference code and bundled third-party libraries that document the project lineage but are not part of the active ESP-IDF build.

## What the firmware does

At runtime the device boots into a short splash/boot sequence, initializes the shared SPI bus, brings up the TFT display, probes three nRF24 radios, starts button polling, and launches three long-running tasks:

- RF scanning task for channel occupancy and directional estimation
- CSI task for Wi-Fi channel-state-based motion/activity sensing
- UI task for screen rendering and user interaction

The UI exposes these modes:

- `RF SCAN`: live channel scanning or track mode using three nRF24 modules
- `CSI`: Wi-Fi CSI activity view using the ESP32 radio
- `ANALYZER`: combined RF/CSI visualizations
- `STATUS`: hardware and runtime health view
- `SETTINGS`: brightness, animation speed, scan dwell, smoothing, theme, sound flag
- `ABOUT`: static device information

## Active repo structure

```text
RFscanner/
|-- CMakeLists.txt                 # ESP-IDF project entry
|-- sdkconfig                      # ESP-IDF configuration
|-- main/
|   |-- CMakeLists.txt             # Main app component definition
|   `-- main.c                     # Boot sequence and task startup
|-- components/
|   |-- animations/                # Small timing helpers for UI animation
|   |-- buttons/                   # GPIO button polling and debounce
|   |-- csi/                       # Wi-Fi CSI capture and activity estimation
|   |-- drivers/                   # Board-level pin map
|   |-- nrf24/                     # nRF24L01 low-level driver
|   |-- rf/                        # RF scanning logic using 3 radios
|   |-- screens/                   # Screen-by-screen rendering logic
|   |-- spi_manager/               # Shared SPI bus setup and locking
|   |-- system/                    # Global app state and mode machine
|   |-- system_state/              # Older state container, mostly superseded
|   |-- tft_display/               # ST7735-style TFT display driver
|   |-- ui/                        # UI refresh loop / render scheduling
|   |-- utils/                     # Small math/index helpers
|   `-- widgets/                   # Reusable UI drawing primitives
|-- PJ/
|   `-- code/                      # Legacy Arduino code and vendored libraries
|-- .devcontainer/                 # Dev container setup
`-- .vscode/                       # VS Code project settings
```

## Build system

The active firmware is a standard ESP-IDF project:

- Root `CMakeLists.txt` includes `project.cmake`
- `main/CMakeLists.txt` pulls in the top-level runtime components
- Each directory in `components/` registers itself with `idf_component_register(...)`

The `main` component depends on:

- `buttons`
- `csi`
- `drivers`
- `nrf24`
- `rf`
- `spi_manager`
- `system`
- `tft_display`
- `ui`
- `nvs_flash`

## Runtime architecture

### 1. Boot and initialization

The main application entry point is `main/main.c`.

High-level boot flow:

1. Initialize NVS.
2. Initialize the global application state with `app_system_init()`.
3. Initialize the shared SPI bus through `spi_manager_init()`.
4. Initialize the TFT display through `tft_display_init()`.
5. Configure all nRF control pins and probe three radios: left, right, top.
6. Start button polling.
7. Initialize the RF scanner, CSI module, and UI.
8. Start monitor, RF, CSI, and UI tasks.
9. Idle forever while worker tasks drive the system.

### 2. Shared state model

The central state container lives in `components/system/include/app_system.h` and `components/system/app_system.c`.

`app_system` owns:

- current mode
- hardware readiness flags
- uptime, free heap, FPS
- last button pressed
- user settings
- RF scan state
- CSI state

Key design idea:

- worker tasks do their own sensing or rendering work
- they publish summarized state into `app_system`
- the UI renders from a snapshot, rather than reaching into hardware directly

This makes the firmware easier to reason about because sensing, state management, and rendering stay separated.

### 3. Task model

The firmware uses FreeRTOS tasks for major subsystems:

- `sys_monitor` in `app_system`
- `rf_scan` in `rf_scanner`
- `csi_task` in `csi_mode`
- `ui_task` in `ui_manager`
- `buttons_task` in `buttons`

Each task has a focused role:

- `sys_monitor` updates uptime and free heap and auto-exits the boot screen after about 4.2 seconds
- `rf_scan` scans channels or tracks the strongest channel and computes left/right/top strengths and a stabilized direction
- `csi_task` enables Wi-Fi CSI when the mode is active and estimates motion/activity from packet amplitude changes
- `ui_task` pulls snapshots, decides whether a redraw is needed, updates brightness, and renders screens
- `buttons_task` polls GPIO buttons, debounces input, and emits short-press and long-press events

## Component-by-component explanation

### `main/`

Files:

- `main/main.c`

Responsibilities:

- system boot
- ordering of initialization
- hardware object allocation
- wiring callbacks and configuration structs together

This file is orchestration-focused. It does not contain the scanning algorithms or UI drawing logic itself.

### `components/drivers/`

Files:

- `components/drivers/include/device_board.h`
- `components/drivers/drivers.c`

Responsibilities:

- central pin map for the board
- display dimensions

Important pin groups:

- SPI bus pins: SCK/MOSI/MISO
- TFT pins: CS/DC/RESET/BACKLIGHT
- nRF24 pins: CE/CSN for left, right, top modules
- buttons: up, down, confirm

This component is deliberately small so board-specific changes stay localized.

### `components/spi_manager/`

Files:

- `components/spi_manager/include/spi_manager.h`
- `components/spi_manager/spi_manager.c`

Responsibilities:

- initialize the shared SPI bus once
- register multiple SPI devices on the same host
- serialize transactions with a mutex

Why it matters:

- the TFT and all three nRF24 modules share one SPI bus
- the mutex prevents overlapping bus transactions from different tasks

### `components/nrf24/`

Files:

- `components/nrf24/include/nrf24.h`
- `components/nrf24/nrf24.c`

Responsibilities:

- low-level nRF24L01 register and command access
- device probe logic
- receive-mode preparation for energy-style scanning
- channel switching and signal presence reads

Important functions:

- `nrf24_init()`: attaches a radio to the shared SPI bus
- `nrf24_probe_with_report()`: checks if the module is really responding
- `nrf24_prepare_rx()`: configures the radio for passive RX-style sensing
- `nrf24_read_rpd()`: reads Received Power Detector state
- `nrf24_read_status()`: reads the STATUS register

Probe behavior:

- reads several registers
- detects all-`0xFF` and all-`0x00` stuck-bus patterns
- temporarily writes a test channel and checks readback

That makes hardware bring-up more robust than a single read.

### `components/rf/`

Files:

- `components/rf/include/rf_scanner.h`
- `components/rf/rf_scanner.c`

Responsibilities:

- own the RF scan task
- sample all three nRF24 radios
- estimate strength and direction
- maintain occupancy/history buffers for the UI

How the scanner works:

1. Decide whether the RF subsystem is active based on app mode.
2. In `SCAN` mode, advance channel by channel across the 2.4 GHz nRF24 channel map.
3. In `TRACK` mode, stay on the strongest known channel.
4. For each dwell window, sample `RPD` and `STATUS` repeatedly from all three radios.
5. Convert hit counts into percentage-like strengths.
6. Smooth strengths with EMA using the user-selected smoothing percentage.
7. Determine a candidate direction from left/right/top strengths.
8. Stabilize the direction with thresholds, margins, and streak logic.
9. Publish the updated RF state to `app_system`.

Important RF state fields:

- `current_channel`
- `strongest_channel`
- `strongest_intensity`
- `left_strength`, `right_strength`, `top_strength`
- `direction`
- `occupancy[126]`
- `history[32]`

This is the core sensing path for the RF side of the project.

### `components/csi/`

Files:

- `components/csi/include/csi_mode.h`
- `components/csi/csi_mode.c`

Responsibilities:

- own the CSI task
- bring up ESP Wi-Fi in STA mode when CSI mode is active
- enable promiscuous CSI collection
- convert raw CSI traffic into a simple motion/activity metric

How CSI activity is estimated:

1. `csi_rx_callback()` accumulates packet count and signal amplitude from incoming CSI samples.
2. The task periodically snapshots and clears the accumulators.
3. It computes an average amplitude per packet.
4. It maintains EMAs for current level, baseline level, and delta from baseline.
5. It maps the delta to a bounded `activity` value.
6. It stores the rolling waveform history for UI graphs.

Important note:

- CSI requires the ESP-IDF option `CONFIG_ESP_WIFI_CSI_ENABLED`
- if that option is disabled, the code logs a clear error and reports CSI as unavailable

### `components/buttons/`

Files:

- `components/buttons/include/buttons.h`
- `components/buttons/buttons.c`

Responsibilities:

- poll three active-low buttons
- debounce them in software
- detect short press vs long press on confirm

Behavior:

- `UP` and `DOWN` generate press events on the press edge
- `CONFIRM` generates a normal `PRESS` on release if a long press did not already fire
- `CONFIRM` generates `LONG_PRESS` after about 700 ms held

The callback from `main.c` logs the event, stores the last button name, and forwards control to `app_system_handle_button()`.

### `components/system/`

Files:

- `components/system/include/app_system.h`
- `components/system/app_system.c`

Responsibilities:

- global application state ownership
- mode transitions
- settings persistence to NVS
- user input state machine
- runtime monitor task

Mode behavior is centralized here. Examples:

- Boot mode auto-transitions to main menu after about 4200 ms
- Long-press confirm exits most screens back to the main menu
- RF mode confirm toggles scan/track behavior
- CSI mode up/down changes Wi-Fi channel
- Settings confirm mutates the selected setting and persists it

This is the firmware's control plane.

### `components/ui/`

Files:

- `components/ui/include/ui_manager.h`
- `components/ui/ui_manager.c`

Responsibilities:

- own the UI task
- decide when a screen redraw is necessary
- push brightness changes to the display backlight
- compute FPS

Instead of redrawing everything constantly, `ui_manager` compares the current snapshot to cached previous values such as mode changes, menu index changes, RF strength/channel/view changes, CSI packet/activity changes, and settings changes.

That keeps the UI simple while still reducing unnecessary drawing work.

### `components/screens/`

Files:

- `components/screens/include/ui_screens.h`
- `components/screens/ui_screens.c`

Responsibilities:

- render each full screen based on `app_snapshot_t`
- keep screen-specific layout logic out of `ui_manager`

Implemented screen renderers:

- boot screen
- main menu
- RF scan
- CSI
- analyzer
- status
- settings
- about

Notable rendering behaviors:

- RF mode has both bar and radar views
- analyzer has multiple pages
- boot screen simulates a staged initialization sequence

### `components/widgets/`

Files:

- `components/widgets/include/ui_widgets.h`
- `components/widgets/ui_widgets.c`

Responsibilities:

- reusable UI drawing helpers
- theme color selection
- bars, headers, status lines, history graphs, waveforms

Theme behavior:

- driven by `settings.color_theme`
- supports green, cyan, and red/yellow-accented variations

### `components/animations/`

Files:

- `components/animations/include/ui_animations.h`
- `components/animations/ui_animations.c`

Responsibilities:

- tiny time-based helpers for sweep and pulse style effects

Currently it contains:

- `ui_animations_radar_x()`
- `ui_animations_glitch_phase()`
- `ui_animations_pulse()`

### `components/tft_display/`

Files:

- `components/tft_display/include/tft_display.h`
- `components/tft_display/tft_display.c`

Responsibilities:

- initialize the SPI TFT panel
- control the PWM backlight
- expose primitive drawing operations

Implemented primitives:

- clear screen
- fill rectangle
- draw text with a built-in 5x7 bitmap font
- set backlight percentage

The driver is tuned for a common ST7735-like display and is intentionally self-contained so display bring-up changes stay localized.

### `components/utils/`

Files:

- `components/utils/include/app_utils.h`
- `components/utils/app_utils.c`

Responsibilities:

- exponential moving average helper
- `uint8_t` clamp helper
- wrapped index helper

These helpers are reused by RF scanning, CSI, and menu navigation.

### `components/system_state/`

Files:

- `components/system_state/include/system_state.h`
- `components/system_state/system_state.c`

This appears to be an older state-management layer from an earlier architecture. The active firmware is built around `app_system` instead. The code is still present in the repo, but it is not the primary state model used by `main.c`.

## UI and interaction model

The device has three buttons:

- `UP`
- `DOWN`
- `CONFIRM`

Global behavior:

- a long press on `CONFIRM` returns to the main menu from most screens

Per-mode highlights:

- Main menu: `UP`/`DOWN` move selection and `CONFIRM` enters the selected mode
- RF scan: `CONFIRM` toggles between scan and track, or resumes if paused; `UP` selects bar view; `DOWN` selects radar view
- CSI: `CONFIRM` pause/resume and `UP`/`DOWN` change Wi-Fi channel
- Analyzer: `UP`, `DOWN`, and `CONFIRM` cycle pages
- Settings: `UP`/`DOWN` choose a row and `CONFIRM` changes the selected value and saves it

## Data flow

### RF path

`nrf24 driver -> rf_scanner task -> app_system RF snapshot -> ui_manager -> ui_screens/widgets -> TFT`

### CSI path

`Wi-Fi CSI callback -> csi_mode task -> app_system CSI snapshot -> ui_manager -> ui_screens/widgets -> TFT`

### Input path

`GPIO buttons -> buttons task -> callback in main.c -> app_system_handle_button() -> snapshot changes -> UI redraw`

## Settings and persistence

Persistent settings live in NVS under namespace `rfsurv`.

Stored settings:

- brightness percent
- animation speed percent
- color theme
- scan dwell milliseconds
- smoothing percent
- sound enabled flag

Default values are assigned in `app_system_init()` and replaced by NVS values if available.

## Hardware assumptions

The active firmware assumes:

- ESP32-S3 target
- one SPI TFT display
- three separate nRF24L01 modules
- three buttons
- Wi-Fi CSI support enabled in ESP-IDF configuration

The three nRF24 modules are treated as directional sensors:

- left
- right
- top

The RF direction estimate comes from comparing the relative strengths across those three receivers.

## Legacy `PJ/` folder

`PJ/` is useful historical and reference material.

It contains:

- `PJ/code/nRFBox_V2/`: older Arduino-style firmware sources such as `scanner.cpp`, `blescan.cpp`, `jammer.cpp`, and `wifiscan.cpp`
- `PJ/code/Library/RF24/`: bundled RF24 library sources, examples, docs, and datasheets
- `PJ/code/Library/U8g2/`: bundled U8g2 graphics library sources and examples
- `PJ/code/Library/Adafruit_NeoPixel/`: bundled NeoPixel library

This folder helps explain where the current firmware came from:

- the project appears to have evolved from an Arduino/nRFBox-style prototype
- the active ESP-IDF firmware in `main/` and `components/` is a cleaner, modular rewrite

If you are documenting or maintaining the project, treat `PJ/` as reference material unless you intentionally want to port legacy behavior into the active firmware.

## How to extend the firmware

Common extension points:

- Add a new screen by adding a mode in `app_system.h`, updating button handling in `app_system.c`, and adding a renderer in `ui_screens.c`
- Add a new sensor by creating a new component under `components/`, publishing summarized state into `app_system`, and rendering it from the UI layer
- Change board wiring by updating `device_board.h`
- Change display controller behavior by updating `tft_display.c`
- Tune RF or CSI behavior by adjusting thresholds and EMA parameters in `rf_scanner.c` or `csi_mode.c`

## Practical reading order

If you are new to the repo, read in this order:

1. `main/main.c`
2. `components/system/include/app_system.h`
3. `components/system/app_system.c`
4. `components/rf/rf_scanner.c`
5. `components/csi/csi_mode.c`
6. `components/ui/ui_manager.c`
7. `components/screens/ui_screens.c`
8. `components/nrf24/nrf24.c`
9. `components/tft_display/tft_display.c`

That sequence matches the real architecture: boot, state, sensing, UI orchestration, rendering, and hardware drivers.

## Summary

This repository contains two layers:

- the active ESP-IDF firmware for a multi-radio RF/CSI handheld scanner
- the older Arduino/reference material preserved under `PJ/`

The active code is structured cleanly around:

- hardware abstraction
- task-based sensing
- centralized application state
- snapshot-driven rendering

That makes the project much easier to maintain and extend than a monolithic single-file prototype.
