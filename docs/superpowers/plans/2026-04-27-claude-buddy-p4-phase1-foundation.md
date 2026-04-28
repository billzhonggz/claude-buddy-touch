# Phase 1: Foundation — Display, BLE, JSON Parsing

> **Last updated**: 2026-04-28 — Tasks 1-3 complete with IDF v6.0 adaptations, BLE unblocked

**Goal:** Boot ESP32-P4, initialize display + LVGL, connect BLE via C6, receive JSON heartbeat from desktop, display "Connected / idle" status on screen.

**Architecture:** ESP-IDF v6.0 project targeting `esp32p4`, using the Waveshare board-specific BSP component (`esp32_p4_wifi6_touch_lcd_4_3`) for MIPI DSI display + touch init. LVGL initialized directly (bypassed `esp_lvgl_adapter` which is incompatible with IDF v6.0). BLE via `esp_hosted` v2 + `esp_wifi_remote` managed components — C6 runs ESP-Hosted slave firmware (pre-flashed), P4 runs NimBLE host with Hosted HCI VHCI over shared SDIO (pattern from `esp-hosted-mcu/examples/host_nimble_bleprph_host_only_vhci`).

**Tech Stack:** ESP-IDF 6.0, LVGL v9.4, Waveshare BSP (board-specific, IDF-v6.0-adapted), cJSON, FreeRTOS

---

## File Structure (after Phase 1)

```
claude-buddy-touch/
├── CMakeLists.txt                  # Top-level ESP-IDF project
├── partitions.csv                  # Partition table (NVS + factory + storage)
├── sdkconfig.defaults              # ESP-IDF config defaults for esp32p4
├── main/
│   ├── CMakeLists.txt              # Main component build
│   ├── idf_component.yml           # Dependencies (lvgl + esp_hosted + esp_wifi_remote)
│   ├── main.c                      # app_main, display init, hello screen
│   ├── display.c                   # LVGL display init via BSP
│   ├── display.h                   # Display API
│   ├── ble_nus.c                   # BLE NUS service (planned)
│   └── ble_nus.h                   # BLE NUS header (planned)
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
- `main/idf_component.yml` — lvgl/lvgl ^9.2 + esp_hosted ~2 + esp_wifi_remote (matching IDF v6.0 iperf example pattern)

**Key sdkconfig settings:**
- `CONFIG_IDF_TARGET="esp32p4"`, `CONFIG_ESPTOOLPY_FLASHSIZE_32MB`
- `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_SPEED_200M=y`
- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y` (v1.3 chip, CPLL at 360MHz)
- `CONFIG_ESP_TASK_WDT_EN=n` (disabled — full-screen LVGL flush exceeds watchdog timeout)
- `CONFIG_BT_ENABLED=y`, `CONFIG_BT_CONTROLLER_DISABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y`
- `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`, `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`
- LVGL: fonts 12-28, compressed, FreeRTOS integration, 15ms refresh

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
- `main/display.h` — `display_init()`, `display_lock/unlock()`, `display_show_hello()`
- `main/display.c` — calls `bsp_display_start()` + `bsp_display_backlight_on()`
- `main/main.c` — NVS init → display init → hello screen → returns
- `main/CMakeLists.txt` — registers main.c + display.c, REQUIRES esp32_p4_wifi6_touch_lcd_4_3 + nvs_flash

**Result:** Screen shows "Claude Buddy Touch / Connecting..." centered on black background.

**Debugging journey:**
1. `assert failed: esp_clk_init clk.c:104 (res)` — CPU freq 400MHz invalid for P4 v1.3 → fixed to 360MHz
2. `abort() in mipi_dsi_ll_set_phy_pllref_clock_source` — XTAL not a valid DSI PHY clock source → fixed to PLL_F20M
3. `task_wdt: IDLE0` — watchdog timeout during initial LVGL full-screen render → disabled task watchdog

---

## Pending Tasks (Updated 2026-04-28)

### Task 4: C6 BLE via esp_hosted + NimBLE Host

**Status:** ✅ Unblocked — `esp_hosted` v2+ and `esp_wifi_remote` 0.14+ are compatible with IDF v6.0 via managed components.

**Architecture:**
```
P4 (NimBLE host) ←→ [Hosted HCI VHCI] ←→ [SDIO transport] ←→ C6 (ESP-Hosted slave firmware, pre-flashed) → BLE radio
```

**Files to create:**
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

**idf_component.yml changes:**
```yaml
dependencies:
  lvgl/lvgl: "^9.2"
  espressif/esp_wifi_remote:
    version: ">=0.10,<2.0"
    rules:
      - if: "target in [esp32p4, esp32h2]"
  espressif/esp_hosted:
    version: "~2"
    rules:
      - if: "target in [esp32p4, esp32h2]"
  idf:
    version: ">=5.4.0"
```

**app_main() flow:**
1. NVS init
2. Display init
3. `esp_hosted_connect_to_slave()` — initialize SDIO link to C6
4. Optionally: `esp_hosted_get_coprocessor_fwversion()` — verify C6 firmware
5. `esp_hosted_bt_controller_init()` — init BT controller on C6
6. `esp_hosted_bt_controller_enable()` — enable BT controller
7. `nimble_port_init()` — init NimBLE host
8. Configure NimBLE: `ble_hs_cfg`, GATT services, device name "Claude-XXXX"
9. `nimble_port_freertos_init()` — start NimBLE host task
10. Advertise NUS service

**Code pattern:** Follow `esp-hosted-mcu/examples/host_nimble_bleprph_host_only_vhci/main/main.c`

**Verification:** Scan for "Claude-XXXX" advertising using nRF Connect or Claude Desktop.

### Task 5: BLE NUS Service

**Status:** ⏸ Pending (follows Task 4)

**Implementation:**
- GATT NUS service with UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX characteristic: `6e400002-b5a3-f393-e0a9-e50e24dcca9e` (write, desktop→device)
- TX characteristic: `6e400003-b5a3-f393-e0a9-e50e24dcca9e` (notify, device→desktop)
- LE Secure Connections with passkey display (DisplayOnly I/O capability)
- Device name: `Claude-XXXX` (random 4 hex chars from MAC)
- Connection callback updates LVGL status display

### Task 6: data.h JSON Parser Port

**Status:** ⏸ Pending

**Port changes from original:**
- Replace ArduinoJson → cJSON
- Replace `M5.Rtc` refs → `time_t`
- Remove `M5.Imu` dependency
- Replace `Serial` → UART buffer from BLE NUS RX callback

### Task 7: Display "Connected/Idle" Status

**Status:** ⏸ Pending

- LVGL label updates based on BLE connection state from Task 5 callbacks
- States: "Connecting...", "Connected", "Idle" (no sessions), "Active" (with sessions)

### Task 8: Touch Input Stub

**Status:** ⏸ Pending

- Touch init already functional via BSP (`bsp_display_start()` → GT911)
- Stub gesture recognition: tap, long-press

### Task 9: Integration + Demo Mode

**Status:** ⏸ Pending

- Wire all components together: display → BLE → data.h → state machine → display
- Loop-back test without desktop: BLE advertise, self-connect for demo mode
