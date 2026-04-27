# Claude Desktop Buddy — ESP32-P4 Touch [Work In Progress]

A hardware companion device for [Claude Desktop](https://claude.ai) that displays session status, permission prompts, and animated pets on a 4.3-inch touch display.

This is a **platform port** of the excellent [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) from the M5StickC Plus (ESP32, Arduino) to the [Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm) (ESP32-P4, ESP-IDF).

## Hardware

| Component | Spec |
|-----------|------|
| **SoC** | ESP32-P4 (RISC-V dual-core + single-core, 360 MHz) |
| **Co-processor** | ESP32-C6 for WiFi 6 + Bluetooth 5 (LE) |
| **Display** | 4.3" IPS, 480×800, MIPI DSI |
| **Touch** | Capacitive (GT911) |
| **RAM** | 32MB PSRAM (in-package) |
| **Flash** | 32MB NOR |
| **Storage** | Micro SD card slot (SDMMC) |
| **Audio** | ES8311 codec + speaker output |
| **Extras** | MIPI camera connector, USB OTG, 40-pin GPIO header |

## Status

**Phase 1 (Foundation): Tasks 1-3 complete**

- ✅ ESP-IDF project skeleton (CMake, partitions, sdkconfig)
- ✅ Display initialized — LVGL v9 renders on 480×800 MIPI DSI
- ✅ Waveshare BSP adapted for ESP-IDF v6.0
- ❌ BLE — blocked (esp_hosted incompatible with IDF v6.0)
- ❌ Protocol, state machine, GIF pets — not yet started

## Quick Start

### Prerequisites

- [ESP-IDF v6.0](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/) installed
- VS Code with [ESP-IDF Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)

### Build

```bash
idf.py set-target esp32p4
idf.py build
```

### Flash

```bash
idf.py -p COM<port> flash monitor
```

## Project Structure

```
├── CMakeLists.txt                  # ESP-IDF project
├── partitions.csv                  # 32MB partition layout
├── sdkconfig.defaults              # IDF config defaults
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                      # app_main entry
│   ├── display.c/h                 # LVGL display init
│   └── idf_component.yml           # Dependencies
├── components/
│   └── esp32_p4_wifi6_touch_lcd_4_3/  # Waveshare BSP (IDF v6.0 adapted)
└── docs/
    ├── PORTING.md                  # Porting technical reference
    ├── REFERENCE.md                # BLE protocol specification
    └── PROGRESS.md                 # Detailed progress log
```

## BLE Protocol

The device communicates with Claude Desktop via Bluetooth LE using the **Nordic UART Service**:

| Role | UUID |
|------|------|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX (desktop → device) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX (device → desktop) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

Advertises as `Claude-XXXX`. Uses LE Secure Connections with passkey entry.
See `docs/REFERENCE.md` for the full protocol spec.

## Roadmap

| Phase | What | Status |
|-------|------|--------|
| 1 | Display + BLE + JSON parsing foundation | Partial (display done, BLE blocked) |
| 2 | State machine + permission UI + touch interaction | Not started |
| 3 | GIF/ASCII pets + settings + info screens | Not started |
| 4 | Audio, clock, folder push, polish | Not started |

## Acknowledgments

- [Felix Rieseberg](https://github.com/felixrieseberg) for the original [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) project
- [Waveshare](https://www.waveshare.com) for the ESP32-P4 board and BSP
- [Espressif](https://www.espressif.com) for ESP-IDF and the ESP32-P4

## License

All rights reserved.
