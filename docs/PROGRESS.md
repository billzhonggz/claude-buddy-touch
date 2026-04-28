# Porting Progress — Claude Desktop Buddy → ESP32-P4

> **Updated**: 2026-04-28
> **Status**: Phase 1 (Foundation) — Tasks 1-3 complete, display functional. Task 4 code written but blocked: C6 factory firmware does not respond to BT feature control RPC (Req_FeatureControl timeout). BLE requires C6 firmware with BT support compiled in.

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
- **LVGL display** — 480×800 screen shows "Claude Buddy Touch / Connecting..." text

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

---

## Phase 1 Status

| Task | Status | Notes |
|------|--------|-------|
| 1. Project skeleton | ✅ Done | CMakeLists, partitions, sdkconfig, idf_component.yml |
| 2. BSP component | ✅ Done | Waveshare BSP adapted for IDF v6.0 |
| 3. Display via LVGL | ✅ Done | Shows "Claude Buddy Touch / Connecting..." |
| 4. C6 hosted setup (code) | ✅ Done | `ble_nus.c/h` written. Managed components fetched (esp_hosted 2.12.6, esp_wifi_remote). Builds clean. |
| 4b. C6 BT controller init | ❌ Blocked | `esp_hosted_bt_controller_init()` fails: C6 factory firmware times out on `Req_FeatureControl` (RPC 0x183). C6 needs reflash with BT-enabled slave firmware. |
| 5. BLE NUS service | ⏸ Blocked by Task 4b | NUS GATT service pattern from `host_nimble_bleprph_host_only_vhci` example — ready but needs working BT controller |
| 6. data.h port | ⏸ Pending | |
| 7. State display | ⏸ Pending | |
| 8. Touch stub | ⏸ Pending | |
| 9. Integration | ⏸ Pending | |

### BLE Implementation Status

**SDIO link:** ✅ Working. `esp_hosted_connect_to_slave()` succeeds — C6 communicates over SDIO.

**BT controller init:** ❌ Fails. `esp_hosted_bt_controller_init()` sends `Req_FeatureControl` (RPC 0x183) to C6, which times out. Root cause: C6 factory ESP-Hosted slave firmware was compiled without BT support (`#if CONFIG_BT_ENABLED` guarded code in `slave_bt.c` is not compiled in).

**Evidence from managed component sources:**
- `slave/main/slave_bt.c:11-13` — Entire BT handler is `#if CONFIG_BT_ENABLED`
- `slave/main/Kconfig.projbuild:7-10` — `ESP_HOSTED_CP_BT` default depends on `SOC_BT_SUPPORTED`
- `slave/sdkconfig.defaults.esp32c6:7-10` — Default slave config for C6 does include `CONFIG_BT_ENABLED=y` and `CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=y`
- Factory firmware on the Waveshare board was likely built with a minimal config that omits BT

**Fix path:** Reflash C6 with ESP-Hosted slave firmware built from the managed component sources with `CONFIG_BT_ENABLED=y`. Either:
1. **OTA from P4 over SDIO** using `esp_hosted_slave_ota_begin/write/end/activate()` — no wiring changes needed
2. **Direct UART flash** by connecting to C6 UART pins

**Sources:**
- `examples/wifi/iperf/main/idf_component.yml` — canonical managed component versions for P4
- `managed_components/espressif__esp_hosted/examples/host_nimble_bleprph_host_only_vhci/` — reference example with correct init sequence
- `managed_components/espressif__esp_hosted/slave/` — C6 slave firmware source (build with `CONFIG_BT_ENABLED=y`)
- `components/soc/esp32p4/include/soc/soc_caps.h:53` — `SOC_WIRELESS_HOST_SUPPORTED`
- `components/esp_phy/CMakeLists.txt:7-11` — empty PHY for P4 (IDF-7460)

---

## File Structure

```
claude-buddy-touch/
├── CMakeLists.txt                       # ESP-IDF project
├── partitions.csv                       # 32MB partition table
├── sdkconfig.defaults                   # IDF config defaults
├── main/
│   ├── CMakeLists.txt                   # Main component build
│   ├── idf_component.yml                # Dependencies (lvgl + esp_hosted + esp_wifi_remote)
│   ├── main.c                           # app_main entry
│   ├── display.c/h                      # LVGL display init + hello screen
│   │   └── display.h
│   ├── ble_nus.c/h                      # BLE NUS service (planned)
│   ├── uart_driver.c                    # HCI UART transport (if using UART fallback)
│   └── data.h                           # JSON parsing + TamaState (planned)
├── components/
│   └── esp32_p4_wifi6_touch_lcd_4_3/    # Waveshare BSP (patched for IDF v6.0)
├── docs/
│   ├── PORTING.md                       # Porting technical reference
│   ├── REFERENCE.md                     # BLE protocol spec
│   ├── PROGRESS.md                      # This file
│   └── superpowers/
│       ├── specs/
│       │   └── 2026-04-27-claude-buddy-p4-port-design.md
│       └── plans/
│           └── 2026-04-27-claude-buddy-p4-phase1-foundation.md
```
