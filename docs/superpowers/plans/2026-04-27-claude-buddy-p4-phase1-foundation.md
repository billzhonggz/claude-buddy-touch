# Phase 1: Foundation — Display, BLE, JSON Parsing

> **Last updated**: 2026-04-27 — Tasks 1-3 complete with IDF v6.0 adaptations

**Goal:** Boot ESP32-P4, initialize display + LVGL, connect BLE via C6, receive JSON heartbeat from desktop, display "Connected / idle" status on screen.

**Architecture:** ESP-IDF v6.0 project targeting `esp32p4`, using the Waveshare board-specific BSP component (`esp32_p4_wifi6_touch_lcd_4_3`) for MIPI DSI display + touch init. LVGL initialized directly (bypassed `esp_lvgl_adapter` which is incompatible with IDF v6.0). BLE approach TBD — `esp_hosted` + `esp_wifi_remote` are incompatible with IDF v6.0.

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
│   ├── idf_component.yml           # Dependencies (lvgl only)
│   ├── main.c                      # app_main, display init, hello screen
│   ├── display.c                   # LVGL display init via BSP
│   └── display.h                   # Display API
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
- `sdkconfig.defaults` — P4 target with SPIRAM, LVGL, perf optimization, 360MHz CPU
- `main/idf_component.yml` — lvgl/lvgl ^9.2 only (esp_hosted/esp_wifi_remote removed — incompatible)

**Key sdkconfig settings:**
- `CONFIG_IDF_TARGET="esp32p4"`, `CONFIG_ESPTOOLPY_FLASHSIZE_32MB`
- `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_SPEED_200M=y`
- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y` (v1.3 chip, CPLL at 360MHz)
- `CONFIG_ESP_TASK_WDT_EN=n` (disabled — full-screen LVGL flush exceeds watchdog timeout)
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

## Pending Tasks

### Task 4: C6 BLE Hosted — BLOCKED

`espressif/esp_hosted` and `espressif/esp_wifi_remote` are incompatible with ESP-IDF v6.0:
- `esp_hosted`: `#error "Unknown Slave Target"` — no P4 host configuration
- `esp_wifi_remote`: uses removed `esp_interface.h` header

**Unblocking options:**
1. **Flash custom C6 firmware** — write standalone NUS BLE app for C6, communicate with P4 over UART/SDIO
2. **Port esp_hosted to IDF v6.0** — patch the component to support P4 as host
3. **Downgrade to IDF v5.4** — where esp_hosted/esp_wifi_remote are supported
4. **USB serial testing** — skip BLE temporarily, test protocol and UI via UART

### Tasks 5-9: Deferred until BLE is resolved
- BLE NUS service implementation (depends on Task 4 approach)
- data.h JSON parser port
- Full state display with LVGL
- Touch init stub
- Integration and demo mode
