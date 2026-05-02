# Porting Progress — Claude Desktop Buddy → ESP32-P4

> **Updated**: 2026-05-02
> **Status**: Phase 1 (Foundation) — All tasks complete. BLE advertising and connections working. Display shows animated ASCII buddy, session status, and demo mode.

---

## Summary

Porting the [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) firmware from ESP32+M5StickC (Arduino/PlatformIO) to Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 (ESP-IDF).

### Key Architectural Decisions
- **Framework**: ESP-IDF (Arduino unsupported on P4)
- **Display**: LVGL v9 via Waveshare BSP (480×800 MIPI DSI)
- **BLE**: ESP32-C6 coprocessor (via SDIO) — `esp_hosted` v2 + `esp_wifi_remote` via managed components, NimBLE host-only with Hosted HCI (VHCI)
- **Input**: Capacitive touch replaces buttons
- **Storage**: nvs_flash (settings) + SPIFFS/SD card (characters)
- **No IMU** — idle timeout replaces face-down nap
- **IDF Version**: v6.0 — required BSP adaptation (no esp_lvgl_adapter, direct LVGL init)

---

## What Works

- **Project skeleton** — ESP-IDF project, partitions, sdkconfig
- **BSP component** — MIPI DSI display init, touch, backlight, I2C, SD card (adapted for IDF v6.0)
- **LVGL display** — 480×800 full-screen with status bar, ASCII buddy art, session info
- **Touch input** — GT911 capacitive touch via BSP, gesture detection (tap advances demo, long-press toggles demo)
- **Demo mode** — Auto-cycles through 5 Claude scenarios (asleep, idle, busy, attention, completed) every 8s
- **JSON parsing** — cJSON-based data layer with TamaState, demo mode, BLE feed buffer
- **State machine** — PersonaState derivation (idle, busy, attention, celebrate) from session data
- **ASCII buddy** — Capybara-style ASCII art animating per persona state (7 expressions)
- **BLE NUS service** — Advertising as `Claude-XXXX`, accepts incoming connections via Nordic UART Service

---

## IDF v6.0 Adaptations Required

The BSP from Waveshare examples was written for IDF 5.x. These changes were needed:

| Change | Reason |
|--------|--------|
| `driver` → `esp_driver_gpio`, `esp_driver_ledc`, etc. | Driver components split in v6.0 |
| Removed `esp_lvgl_adapter` | Uses `pixel_format_unique_id` renamed to `pixel_format_fourcc_id` |
| Direct LVGL init (`lv_display_create`, `lv_indev_create`) | Replacement for adapter |
| `pixel_format` → `in_color_format`/`out_color_format` | API rename in `esp_lcd_dpi_panel_config_t` |
| `LCD_COLOR_PIXEL_FORMAT_RGB565` → `LCD_COLOR_FMT_RGB565` | Enum rename |
| Removed `use_dma2d` flag | Field removed from DPI config struct |
| `ESP_LCD_COLOR_SPACE_RGB` → `LCD_RGB_ELEMENT_ORDER_RGB` | Enum rename |
| `MIPI_DSI_PHY_CLK_SRC_DEFAULT` → `MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M` | Clock source renamed + XTAL not valid |
| Removed USB code (`usb/usb_host.h`) | USB component removed in v6.0 |
| Removed audio/I2S code (`esp_codec_dev`) | Incompatible with v6.0, not needed yet |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360` (not 400) | P4 v1.3 uses CPLL 360MHz, 400MHz invalid |
| Disabled task watchdog | Initial LVGL full-screen flush exceeds timeout |
| `SOC_WIRELESS_HOST_SUPPORTED=1` not `SOC_WIFI_SUPPORTED` | P4 has no native WiFi — C6 handles all RF |
| `esp_phy` component is empty stub on P4 | P4 has no radio hardware |
| cJSON removed from base | Now a managed component: `espressif/cjson: "^1.7.19~2"` |
| cJSON include changed | `#include "cJSON.h"` via `espressif__cjson` managed component include dirs |

---

## Phase 1 Status

| Task | Status | Notes |
|------|--------|-------|
| 1. Project skeleton | ✅ Done | CMakeLists, partitions, sdkconfig, idf_component.yml |
| 2. BSP component | ✅ Done | Waveshare BSP adapted for IDF v6.0 |
| 3. Display via LVGL | ✅ Done | Shows animated ASCII buddy + status |
| 4. C6 hosted setup (code) | ✅ Done | `ble_nus.c/h` written. Managed components fetched. |
| 4b. C6 BT controller init | ✅ Done | C6 reflashed with ESP-Hosted slave FW v2.12.6 |
| 5. BLE NUS service | ✅ Done | Advertising & connections working via NUS |
| 6. **data.h port** | ✅ **Done** | cJSON-based JSON parsing, TamaState, demo mode, BLE line buffer |
| 7. **State display** | ✅ **Done** | Dynamic status: Connecting/Connected/Idle/Active with colored dot |
| 8. **Touch stub** | ✅ **Done** | Tap (advance demo), long-press (toggle demo) |
| 9. **Integration** | ✅ **Done** | State-driven app task, demo mode, BLE fallback |

### BLE Implementation Status

**SDIO link:** ✅ Working. `esp_hosted_connect_to_slave()` succeeds.

**C6 capabilities:** C6 reports `capabilities: 0xd` = WLAN + HCI over SDIO + BLE only. `vhci_drv: Host BT Support: Enabled, BT Transport Type: VHCI` confirms P4-side VHCI driver is ready.

**C6 firmware:** ✅ Updated to ESP-Hosted slave FW v2.12.6 (was factory v0.0.6). RPC protocol version mismatch resolved.

**BT controller init:** ✅ `esp_hosted_bt_controller_init()` and `esp_hosted_bt_controller_enable()` succeed.

**BLE NUS service:** ✅ Advertising as `Claude-XXXX`, accepts incoming connections. Two issues resolved during bringup:
  1. **`ble_gatts_count_cfg rc=3`** (`BLE_HS_EINVAL`) — TX characteristic had `.access_cb = NULL`. NimBLE requires non-NULL access callbacks on all characteristics. Fixed by providing a shared access callback.
  2. **`ble_gap_adv_set_fields rc=4`** (`BLE_HS_EMSGSIZE`) — Advertisement data (flags + tx_power + name + 128-bit UUID) exceeded 31-byte limit. Fixed by moving 128-bit UUID to scan response.

---

## File Structure

```
claude-buddy-touch/
├── CMakeLists.txt                       # ESP-IDF project
├── partitions.csv                       # 32MB partition table
├── sdkconfig.defaults                   # IDF config defaults
├── main/
│   ├── CMakeLists.txt                   # Main component build
│   ├── idf_component.yml                # Dependencies (lvgl, cjson, esp_hosted)
│   ├── main.c                           # App task, state machine, touch + demo loop
│   ├── display.c/h                      # LVGL screen: buddy art, status labels
│   ├── tama_state.h                     # TamaState struct definition
│   ├── data.h                           # JSON parsing, demo mode, BLE buffer (header-only)
│   ├── touch.c/h                        # Gesture detection (tap, long-press)
│   ├── ble_nus.c/h                      # BLE NUS service (advertising, connected)
├── components/
│   └── esp32_p4_wifi6_touch_lcd_4_3/    # Waveshare BSP (patched for IDF v6.0)
├── managed_components/
│   ├── espressif__cjson/                # cJSON managed component
│   ├── espressif__esp_hosted/           # ESP-Hosted SDIO transport
│   ├── espressif__esp_wifi_remote/      # WiFi remote library
│   ├── espressif__esp_lcd_st7701/       # ST7701 display driver
│   ├── espressif__esp_lcd_touch/        # Touch abstraction
│   ├── espressif__esp_lcd_touch_gt911/  # GT911 touch driver
│   └── lvgl__lvgl/                      # LVGL v9.4
├── docs/
│   ├── PROGRESS.md                      # This file
│   └── superpowers/
│       ├── specs/
│       │   └── 2026-04-27-claude-buddy-p4-port-design.md
│       └── plans/
│           └── 2026-04-27-claude-buddy-p4-phase1-foundation.md
```
