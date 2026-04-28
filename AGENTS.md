# AGENTS.md — Claude Buddy Touch

## Project

Port of [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) (M5StickC Plus, Arduino) to the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3** (ESP-IDF v6.0). BLE companion device for Claude Desktop.

## Build & Flash

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p COM<port> flash monitor    # UART flash (COM4 on dev machine)
```

- VS Code uses `C:\esp\v6.0\esp-idf`, target `esp32p4`, flash via COM4 UART.
- `.clangd` strips `-f*` / `-m*` flags from compile commands (generated in `build/`).

## Key Architecture

| Layer | Details |
|-------|---------|
| Entry | `main/main.c` → `app_main()`, NVS boot, then display init |
| Display | `display.c/h` wraps Waveshare BSP `bsp_display_start()` + LVGL v9.4 |
| BSP | `components/esp32_p4_wifi6_touch_lcd_4_3/` — custom-adapted from Waveshare for IDF v6.0 |
| Touch | GT911 capacitive (replaces M5StickC hardware buttons) |
| BLE | ESP32-C6 coprocessor via SDIO — **unblocked**. Uses `esp_hosted` v2 + `esp_wifi_remote` managed components. P4 runs NimBLE host with Hosted HCI VHCI over shared SDIO transport. C6 runs ESP-Hosted slave firmware (pre-flashed from factory). Pattern: `esp-hosted-mcu/examples/host_nimble_bleprph_host_only_vhci` |
| Storage | `nvs_flash` (settings) + SPIFFS partition `"storage"` → `/spiffs` + SDMMC → `/sdcard` |
| Partition | 32MB flash: factory 16MB, storage (SPIFFS) ~15.7MB |
| Dependencies | `lvgl/lvgl 9.4.0`, `esp_lcd_st7701 2.0.2`, `esp_lcd_touch_gt911 1.2.0~2`, `espressif/esp_hosted ~2`, `espressif/esp_wifi_remote >=0.10,<2.0` (via managed_components) |

## IDF v6.0 Quirks

- Driver components are split: use `esp_driver_gpio`, `esp_driver_ledc`, etc. (not `driver`)
- No `esp_lvgl_adapter` — init LVGL directly (`lv_display_create`, `lv_indev_create`)
- DPI config uses `in_color_format`/`out_color_format` (not `pixel_format`)
- `LCD_COLOR_FMT_RGB565` not `LCD_COLOR_PIXEL_FORMAT_RGB565`
- `MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M` not `MIPI_DSI_PHY_CLK_SRC_DEFAULT`
- USB (`usb/usb_host.h`) and audio/I2S/`esp_codec_dev` removed from v6.0
- P4 rev 1.3 runs at 360 MHz (not 400 MHz — `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360`)
- Task WDT disabled (`CONFIG_ESP_TASK_WDT_EN=n`) — LVGL init flush exceeds timeout
- `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`
- `SOC_WIRELESS_HOST_SUPPORTED=1` on P4 (not `SOC_WIFI_SUPPORTED`) — no native WiFi/BT, `esp_phy` is empty stub
- C6 communicates via SDIO (4-bit, 40MHz) using `esp_hosted` transport
- BLE uses Hosted HCI VHCI — NimBLE host ↔ SDIO ↔ C6 controller (no extra GPIOs)
- BT controller init: `esp_hosted_connect_to_slave()` → `esp_hosted_bt_controller_init()` → `esp_hosted_bt_controller_enable()`

## BLE Protocol (stable contract)

Nordic UART Service, JSON lines (`\n`-delimited), documented in `docs/REFERENCE.md`.

| UUID | Purpose |
|------|---------|
| `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | NUS Service |
| `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | RX (desktop → device) |
| `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | TX (device → desktop, notify) |

Advertises as `Claude-XXXX`. LE Secure Connections with passkey entry.

## Porting Status

**Phase 1 (Foundation):** Tasks 1-3 complete. Task 4 code complete but runtime blocked.

**BLE Status:** SDIO link to C6 works (`esp_hosted_connect_to_slave()` succeeds), C6 reports capabilities including HCI and BLE, but `esp_hosted_bt_controller_init()` fails due to RPC protocol version mismatch (host ESP-Hosted 2.12.0 vs C6 firmware reporting 0.0.0). The C6 firmware needs updating to match the host's RPC protocol. See `docs/PROGRESS.md` for full details.

**SDK config** includes: `BT_ENABLED`, `BT_CONTROLLER_DISABLED`, `BT_NIMBLE_ENABLED`, `ESP_HOSTED_ENABLE_BT_NIMBLE`, `ESP_HOSTED_NIMBLE_HCI_VHCI`.

**BLE code pattern:** Follows `managed_components/espressif__esp_hosted/examples/host_nimble_bleprph_host_only_vhci/main/main.c`

**C6 firmware fix:** Reflash C6 with ESP-Hosted slave firmware from `managed_components/espressif__esp_hosted/slave/` built at a version matching the host. Options: SDIO OTA from P4, or direct UART flash.

## Key Design Choices

- Capacitive touch replaces button hardware (tap vs long-press mapping TBD)
- No IMU — not present on this board
- ASCII pets (buddy.cpp) and GIF characters (character.cpp) from original are portable C++ — no hardware deps
- Original `data.h` (JSON parsing, session state machine) is 100% portable
- `docs/PORTING.md` has the full platform porting reference from the original project

## Original Source Reference

Original firmware lives at `C:\Users\Junru\source\claude-desktop-buddy` — refer there for BLE bridge logic, state machine, data layer, ASCII pets, GIF rendering, stats, and folder push protocol that still needs to be ported.
