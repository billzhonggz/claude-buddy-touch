# Phase 1: Foundation — Display, BLE, JSON Parsing

> **Last updated**: 2026-04-30 — Tasks 1-3, 6-9 complete. Demo mode working with ASCII buddy, touch cycling, and session status. BLE (Tasks 4-5) blocked by C6 firmware RPC version mismatch.

**Goal:** Boot ESP32-P4, initialize display + LVGL, connect BLE via C6, receive JSON heartbeat from desktop, display "Connected / idle" status on screen.

**Architecture:** ESP-IDF v6.0 project targeting `esp32p4`, using the Waveshare board-specific BSP component (`esp32_p4_wifi6_touch_lcd_4_3`) for MIPI DSI display + touch init. LVGL initialized directly (bypassed `esp_lvgl_adapter` which is incompatible with IDF v6.0). BLE via `esp_hosted` v2 + `esp_wifi_remote` managed components — C6 runs ESP-Hosted slave firmware (pre-flashed), P4 runs NimBLE host with Hosted HCI VHCI over shared SDIO (pattern from `esp-hosted-mcu/examples/host_nimble_bleprph_host_only_vhci`).

**Tech Stack:** ESP-IDF 6.0, LVGL v9.4, Waveshare BSP (board-specific, IDF-v6.0-adapted), cJSON (managed component `espressif/cjson`), FreeRTOS

---

## File Structure (after Phase 1)

```
claude-buddy-touch/
├── CMakeLists.txt                  # Top-level ESP-IDF project
├── partitions.csv                  # Partition table (NVS + factory + storage)
├── sdkconfig.defaults              # ESP-IDF config defaults for esp32p4
├── main/
│   ├── CMakeLists.txt              # Main component build
│   ├── idf_component.yml           # Dependencies (lvgl, cjson, esp_hosted, esp_wifi_remote)
│   ├── main.c                      # app_main, app task, state machine, touch loop
│   ├── display.c                   # LVGL display: buddy art, status labels
│   ├── display.h                   # Display API + PersonaState enum
│   ├── tama_state.h                # TamaState struct definition
│   ├── data.h                      # JSON parsing (cJSON), demo mode, BLE buffer
│   ├── touch.c                     # Gesture detection (tap, long-press)
│   ├── touch.h                     # Touch event types and API
│   ├── ble_nus.c                   # BLE NUS service (blocked, compiles)
│   └── ble_nus.h                   # BLE NUS header
├── components/
│   └── esp32_p4_wifi6_touch_lcd_4_3/   # Board BSP (adapted for IDF v6.0)
└── docs/
    └── superpowers/
        ├── specs/
        │   └── 2026-04-27-claude-buddy-p4-port-design.md
        └── plans/
            └── 2026-04-27-claude-buddy-p4-phase1-foundation.md
```

---

## Completed Tasks

### Task 1: Create ESP-IDF Project Skeleton ✅

**Files:**
- `CMakeLists.txt` — ESP-IDF project boilerplate
- `partitions.csv` — nvs(0x9000/0x6000), phy_init(0xF000/0x1000), factory(0x10000/0x1000000), storage(0xFB0000)
- `sdkconfig.defaults` — P4 target with SPIRAM, LVGL, perf optimization, 360MHz CPU, BT+HCI configs
- `main/idf_component.yml` — lvgl/lvgl ^9.2 + esp_hosted ~2 + esp_wifi_remote + espressif/cjson

**Key sdkconfig settings:**
- `CONFIG_IDF_TARGET="esp32p4"`, `CONFIG_ESPTOOLPY_FLASHSIZE_32MB`
- `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_SPEED_200M=y`
- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y` (v1.3 chip, CPLL at 360MHz)
- `CONFIG_ESP_TASK_WDT_EN=n` (disabled — full-screen LVGL flush exceeds watchdog timeout)
- `CONFIG_BT_ENABLED=y`, `CONFIG_BT_CONTROLLER_DISABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y`
- `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`, `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`
- LVGL: fonts 12-28+36, compressed, FreeRTOS integration, 15ms refresh

---

### Task 2: Board BSP Component for Display + Touch ✅

**Source:** `C:\Users\Junru\source\esp32p4-dev\ESP32-P4-WIFI6-Touch-LCD-4.3\examples\esp-idf\07_Displaycolorbar\components\esp32_p4_wifi6_touch_lcd_4_3\`

**Location:** `components/esp32_p4_wifi6_touch_lcd_4_3/`

**IDF v6.0 adaptations (original BSP written for IDF 5.x):**

#### CMakeLists.txt changes
- Replaced `REQUIRES driver` → `REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_sdmmc esp_driver_spi esp_hw_support sdmmc esp_timer`
- Moved `esp_lcd` from `PRIV_REQUIRES` → `REQUIRES` (public headers include esp_lcd_types.h)

#### BSP source changes (`esp32_p4_wifi6_touch_lcd_4_3.c`)

**Removed incompatible code:**
- USB host functions (`usb/usb_host.h` — component removed in IDF v6.0)
- Audio/I2S functions (`esp_codec_dev` — incompatible version; deferred)
- `esp_lvgl_adapter` dependency (uses removed `pixel_format_unique_id` API)

**Fixed deprecated APIs:**
- `.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565` → `.in_color_format = LCD_COLOR_FMT_RGB565, .out_color_format = LCD_COLOR_FMT_RGB565`
- `.flags.use_dma2d = true` → removed (field no longer exists)
- `MIPI_DSI_PHY_CLK_SRC_DEFAULT` → `MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M` (XTAL/DEFAULT not valid)
- `ESP_LCD_COLOR_SPACE_RGB` → `LCD_RGB_ELEMENT_ORDER_RGB`

**Direct LVGL initialization (replacing esp_lvgl_adapter):**
- `lv_init()` before any LVGL API calls
- `lv_display_create()` + `lv_display_set_buffers()` + `lv_display_set_flush_cb()`
- Custom `lvgl_flush_cb` calling `esp_lcd_panel_draw_bitmap()`
- Custom `lvgl_touch_read_cb` using `esp_lcd_touch_read_data()`/`esp_lcd_touch_get_coordinates()`
- `lv_tick_inc(1)` via `esp_timer` at 1ms
- `lv_timer_handler()` via dedicated FreeRTOS task at 5ms interval

---

### Task 3: Display "Hello" via LVGL ✅

**Files:**
- `main/display.h` — `display_init()`, `display_lock/unlock()`, `display_show_hello()`, `display_update()`
- `main/display.c` — calls `bsp_display_start()` + `bsp_display_backlight_on()`, persistent UI labels + ASCII buddy
- `main/main.c` — NVS init → display init → hello screen → app task

**Result:** Screen shows animated ASCII buddy with connection status, session counts, and persona state.

**Debugging journey:**
1. `assert failed: esp_clk_init clk.c:104 (res)` — CPU freq 400MHz invalid for P4 v1.3 → fixed to 360MHz
2. `abort() in mipi_dsi_ll_set_phy_pllref_clock_source` — XTAL not a valid DSI PHY clock source → fixed to PLL_F20M
3. `task_wdt: IDLE0` — watchdog timeout during initial LVGL full-screen render → disabled task watchdog

---

## Completed Tasks

### Task 4: C6 BLE via esp_hosted + NimBLE Host (code complete, runtime blocked)

**Files created:**
- `main/ble_nus.h` — BLE NUS service declarations, Claude protocol GATT service UUIDs
- `main/ble_nus.c` — NimBLE host initialization, GATT server with NUS service, passkey handler, connection callbacks

**sdkconfig changes:**
```ini
CONFIG_BT_ENABLED=y
CONFIG_BT_CONTROLLER_DISABLED=y
CONFIG_BT_BLUEDROID_ENABLED=n
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_TRANSPORT_UART=n
CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED=y
CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y
CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y
```

**app_main() flow:**
1. NVS init
2. Display init
3. `esp_hosted_connect_to_slave()` — SDIO link: ✅ works
4. `esp_hosted_get_coprocessor_fwversion()` — verify C6 firmware: ✅ works
5. `esp_hosted_get_cp_info()` — verify C6 chip: ✅ works
6. `esp_hosted_bt_controller_init()` — **❌ FAILS**: RPC protocol mismatch
7. `esp_hosted_bt_controller_enable()` — not reached

### Task 5: BLE NUS Service ⏸

Blocked by Task 4.

### Task 6: data.h JSON Parser Port ✅

**Files:**
- `main/tama_state.h` — TamaState struct definition (shared struct)
- `main/data.h` — Header-only data layer (included only from main.c)

**Port changes from original:**
- ArduinoJson → cJSON (managed component `espressif/cjson`)
- `M5.Rtc` refs → `time_t` stored in static variable
- Removed `M5.Imu` dependency
- `Serial` BLE polling → `data_feed_byte()` / `data_feed()` called from BLE RX callback
- `millis()` → `esp_timer_get_time() / 1000`
- Arduino `Stream` line buffer → simple char buffer with `\n`/`\r` detection
- Demo mode: 5 scenarios auto-cycling every 8 seconds

### Task 7: Display "Connected/Idle" Status ✅

- Dynamic LVGL labels updated at 20fps via `display_update()`
- Connection dot color: red (disconnected), blue (connected), green (active), yellow (prompt)
- Status text: "Connecting...", "Connected", "Idle", "Active", "Prompt waiting!"
- ASCII buddy art (capybara-style) rendered with `lv_font_montserrat_28`, centered, state-driven expressions
- Session counts, message, tokens, and persona state labels

### Task 8: Touch Input Stub ✅

**Files:**
- `main/touch.h` — touch_event_t enum, touch_event_data_t struct
- `main/touch.c` — gesture detection using `bsp_display_get_input_dev()` LVGL indev

**Gestures:**
- Tap (<300ms): advance to next demo scenario
- Long-press (>500ms): toggle demo mode on/off
- Swipe detection (armed but demo just advances in all directions)

### Task 9: Integration + Demo Mode ✅

- FreeRTOS app task loops at 20fps: `data_poll()` → `derive_state()` → `display_update()`
- BLE init failure is non-fatal — app runs in standalone demo mode
- Tap cycles through 5 scenarios (asleep → one idle → busy → attention → completed)
- Long-press toggles between demo and "Connecting..." (no BLE data) state
- State machine from `TamaState` to `PersonaState`: idle, busy (≥3 running), attention (≥1 waiting), celebrate (completed)
