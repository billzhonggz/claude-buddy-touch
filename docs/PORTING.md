# Claude Desktop Buddy — Platform Porting Reference

> **Status**: Draft Technical Reference
> **Source Code**: `C:\Users\Junru\source\claude-desktop-buddy`
> **Last Updated**: 2026-04-27

---

## 1. Project Overview

**Project Type**: Embedded firmware for a hardware BLE companion device
**Current Platform**: ESP32 (M5StickC Plus) with Arduino framework
**Build System**: PlatformIO Core

### Purpose
A "desk pet" device that connects to Claude desktop apps via Bluetooth Low Energy (BLE) to:
- Display permission prompts and session status
- Act as a physical approval interface for AI tool executions
- Show animated characters (ASCII pets or GIF sprites)

### Core Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                     Claude Desktop App                       │
│                   (macOS / Windows)                          │
└─────────────────────────┬───────────────────────────────────┘
                          │ BLE Nordic UART Service
                          │ (6e400001-b5a3-f393-e0a9-e50e24dcca9e)
┌─────────────────────────▼───────────────────────────────────┐
│                    ESP32 Firmware                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ BLE Bridge   │  │ State       │  │ Display             │ │
│  │ (ble_bridge)│  │ Machine      │  │ (M5StickCPlus)      │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ Data Layer   │  │ Character   │  │ Persistence         │ │
│  │ (data.h)     │  │ (character)  │  │ (stats.h/NVS)       │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Source Code Map

### Directory Structure

| Path | Purpose | Portable? |
|------|---------|----------|
| `src/main.cpp` | Main loop, state machine, UI orchestration | No |
| `src/ble_bridge.cpp/h` | BLE server (Nordic UART Service) | **Yes** |
| `src/data.h` | JSON parsing, session state, wire protocol | **Yes** |
| `src/buddy.cpp/h` | ASCII pet species dispatch and rendering | **Yes** |
| `src/buddies/*.cpp` | 18 ASCII pet species (one file per species) | **Yes** |
| `src/character.cpp/h` | GIF decode and sprite rendering | **Yes** |
| `src/stats.h` | NVS-backed stats, settings, owner/species name | Partial |
| `src/xfer.h` | BLE folder push receiver for character install | **Yes** |
| `src/data.h` | Session state machine, JSON parsing | **Yes** |
| `platformio.ini` | Build configuration | No |
| `characters/bufo/` | Example GIF character pack | **Yes** |
| `tools/` | Python scripts (character prep, flashing) | **Yes** |

### File Dependencies

```
main.cpp
├── M5StickCPlus.h        [HARDWARE - display, IMU, power, buttons]
├── LittleFS.h            [HARDWARE - file system]
├── ble_bridge.h         [PORTABLE - BLE protocol]
├── data.h               [PORTABLE - JSON parsing, session state]
├── buddy.h              [PORTABLE - ASCII pet dispatch]
├── character.h          [PORTABLE - GIF rendering]
└── stats.h              [PARTIAL - replace NVS/Preferences]

ble_bridge.cpp
├── BLEDevice.h          [HARDWARE - ESP32 BLE stack]
├── BLEServer.h          [HARDWARE - ESP32 BLE stack]
├── BLEUtils.h           [HARDWARE - ESP32 BLE stack]
├── BLESecurity.h        [HARDWARE - ESP32 BLE stack]
└── ESP32 APIs          [HARDWARE - esp_read_mac, etc.]

stats.h
├── Preferences.h        [HARDWARE - NVS abstraction]
└── Arduino.h            [HARDWARE - basic types]
```

---

## 3. Component Portability Analysis

### 3.1 Portable Components

#### BLE Bridge (`src/ble_bridge.cpp`)
- **Status**: ~90% portable
- **Keep**: All UUIDs, BLE server architecture, RX/TX ring buffer, MTU handling
- **Replace**: ESP32-specific includes and APIs:
  - `esp_read_mac()` → platform MAC address API
  - `esp_ble_get_bond_device_num()` → platform bonding API
  - `esp_ble_bond_dev_t` → platform bond structure
  - `ESP_BLE_SEC_ENCRYPT_MITM`, `ESP_IO_CAP_OUT` → platform security constants
  - `ESP.getFreeHeap()` → remove or replace with platform equivalent

#### Data Layer (`src/data.h`)
- **Status**: 100% portable
- **Contains**: JSON parsing with ArduinoJson, session state machine
- **Dependencies**: ArduinoJson library (cross-platform)

#### ASCII Pets (`src/buddy.cpp`, `src/buddies/*.cpp`)
- **Status**: 100% portable
- **Contains**: Text-based rendering to any canvas
- **No hardware dependencies**

#### Character/GIF Rendering (`src/character.cpp/h`)
- **Status**: Portable with library swap
- **Current**: Uses `bitbank2/AnimatedGIF` library
- **Replacement**: Any GIF decoder that outputs to pixel buffer

#### Folder Push Protocol (`src/xfer.h`)
- **Status**: 100% portable
- **Contains**: BLE file transfer state machine
- **No hardware dependencies**

### 3.2 Hardware-Dependent Components

#### Display (`main.cpp` display sections)
- **Current**: M5StickCPlus TFT (135x240 pixels)
- **Dependencies**:
  - `TFT_eSprite` for double-buffered rendering
  - `M5.Lcd` for direct writes
  - Screen brightness via `M5.Axp.ScreenBreath()`
  - Color format: RGB565 (uint16_t)

#### IMU (Inertial Measurement Unit)
- **Current**: MPU6886 via M5StickCPlus library
- **Used for**:
  - `isFaceDown()` — nap detection (az < -0.7)
  - `checkShake()` — shake gesture detection
  - `clockUpdateOrient()` — landscape/portrait detection
- **Replacement**: Any accelerometer with I2C/SPI interface

#### Power Management
- **Current**: AXP192 power IC via M5StickCPlus library
- **Used for**:
  - `M5.Axp.ScreenBreath()` — backlight control
  - `M5.Axp.SetLDO2()` — screen power on/off
  - `M5.Axp.GetBatVoltage()` / `GetBatCurrent()` — battery info
  - `M5.Axp.GetVBusVoltage()` — USB detection
  - `M5.Axp.GetTempInAXP192()` — temperature
  - `M5.Axp.GetBtnPress()` — power button
  - `M5.Axp.PowerOff()` — device shutdown
- **Replacement**: Platform-specific power management

#### Buttons
- **Current**: M5StickCPlus button handling
- **Used for**: `M5.BtnA`, `M5.BtnB` with multi-stage detection (tap vs long-press)
- **Replacement**: Any button GPIO input with debouncing

#### Buzzer
- **Current**: `M5.Beep.tone()` for audio feedback
- **Replacement**: Platform PWM or GPIO buzzer

#### RTC (Real-Time Clock)
- **Current**: M5StickCPlus RTC synced via BLE
- **Used for**: Clock display with orientation-aware rendering
- **Replacement**: Platform RTC or system time

#### File System
- **Current**: LittleFS on ESP32 flash
- **Used for**: GIF character storage in `/characters/` directory
- **Replacement**: Any persistent file system (FAT, SPIFFS, etc.)

#### Non-Volatile Storage (NVS)
- **Current**: `Preferences` library wrapping ESP32 NVS
- **Used for**: Stats, settings, pet name, owner name, species selection
- **Replacement**: Any key-value store (EEPROM, flash, file-based)

#### LED
- **Current**: GPIO 10, active-low red LED
- **Replacement**: Any GPIO output

---

## 4. BLE Protocol — The Stable Contract

> **IMPORTANT**: The BLE protocol defined in `REFERENCE.md` is the stable surface. Any device implementing Nordic UART Service with this protocol can communicate with Claude desktop apps.

### 4.1 Service UUIDs

| Role | UUID |
|------|-----|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX (desktop → device, write) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX (device → desktop, notify) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

### 4.2 Advertising Name
Advertise as `Claude-XXXX` (last two BT MAC bytes) so multiple devices are distinguishable.

### 4.3 Transport
- UTF-8 JSON, one object per line, terminated with `\n`
- Desktop handles multi-packet reassembly
- Device must accumulate bytes until `\n`, then parse

### 4.4 Key Messages

#### Heartbeat Snapshot (desktop → device)
```json
{
  "total": 3,
  "running": 1,
  "waiting": 1,
  "msg": "approve: Bash",
  "entries": ["10:42 git push", "10:41 yarn test"],
  "tokens": 184502,
  "tokens_today": 31200,
  "prompt": {
    "id": "req_abc123",
    "tool": "Bash",
    "hint": "rm -rf /tmp/foo"
  }
}
```

#### Permission Decision (device → desktop)
```json
{"cmd":"permission","id":"req_abc123","decision":"once"}
{"cmd":"permission","id":"req_abc123","decision":"deny"}
```

#### Time Sync (desktop → device on connect)
```json
{ "time": [1775731234, -25200] }
```

#### Status Response (device → desktop)
```json
{
  "ack": "status",
  "ok": true,
  "data": {
    "name": "Clawd",
    "sec": true,
    "bat": { "pct": 87, "mV": 4012, "mA": -120, "usb": true },
    "sys": { "up": 8412, "heap": 84200 },
    "stats": { "appr": 42, "deny": 3, "vel": 8, "nap": 12, "lvl": 5 }
  }
}
```

### 4.5 Security
- LE Secure Connections bonding with passkey entry
- Device displays 6-digit passkey, user enters on desktop
- Link AES-CCM encrypted once bonded
- Include `"sec": true` in status ack when encrypted

---

## 5. Porting Guides

### 5.1 Porting to Another ESP32 Board

**Difficulty**: Low | **Timeline**: 1-2 weeks

#### Steps

1. **Create new PlatformIO environment** in `platformio.ini`:
   ```ini
   [env:your-board]
   platform = espressif32
   board = your_board_id
   framework = arduino
   ```

2. **Replace M5Stack library** with your board's display/IMU library

3. **Adapt display dimensions** in `main.cpp`:
   ```cpp
   const int W = YOUR_WIDTH, H = YOUR_HEIGHT;
   const int CX = W / 2;
   const int CY_BASE = H / 2;
   ```

4. **Update LED pin**:
   ```cpp
   const int LED_PIN = YOUR_LED_GPIO;
   ```

5. **Replace power management calls**:
   - `M5.Axp.ScreenBreath(b)` → your backlight API
   - `M5.Axp.SetLDO2(on)` → your screen power API
   - `M5.Axp.GetBatVoltage()` → your battery ADC
   - `M5.Axp.PowerOff()` → your power-off sequence

6. **Replace IMU calls**:
   - `M5.Imu.getAccelData(&ax, &ay, &az)` → your accelerometer API
   - Tune thresholds for `isFaceDown()` and `checkShake()`

7. **Replace button handling** with your GPIO input library

8. **Replace buzzer** with your PWM/GPIO tone API

9. **Update build flags** if needed for your board's partition layout

#### What Stays the Same
- `ble_bridge.cpp` — BLE protocol unchanged
- `data.h` — JSON parsing unchanged
- `buddy.cpp` / `buddies/*.cpp` — ASCII pets unchanged
- `character.cpp` — GIF rendering unchanged (swap AnimatedGIF if needed)
- `xfer.h` — folder push unchanged
- `stats.h` — Preferences library works on any ESP32

---

### 5.2 Porting to nRF52 (Nordic)

**Difficulty**: Medium | **Timeline**: 2-4 weeks

#### Architecture Changes

1. **Replace ESP32 BLE with nRF BLE stack**:
   - Use Nordic's `ble_nus.c` (Nordic UART Service) or implement from scratch
   - Maintain same UUIDs: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
   - Implement LE Secure Connections with passkey

2. **Replace Arduino framework**:
   - Option A: Keep Arduino framework with nRF52 boards support
   - Option B: Use Nordic SDK directly (Zephyr or bare metal)

3. **Adapt BLE security**:
   - Use nRF's `ble_opt` and `ble_gap_sec_params_t`
   - Implement `BLE_GAP_IO_CAPS_DISPLAY_ONLY` for passkey display

4. **Replace hardware calls**:
   - Display: Use Adafruit GFX or LVGL with nRF SPI
   - IMU: Use platform I2C driver for your accelerometer
   - Flash: Use nRF's QSPI or external SPI flash for LittleFS
   - NVS: Use nRF's `nrf_fstorage` or `pstorage`

#### Key Porting Points in `ble_bridge.cpp`

```c
// ESP32 → nRF equivalent
esp_read_mac()              → use FICR or manual MAC
esp_ble_get_bond_device_num() → sd_ble_gap_sec_get()
esp_ble_bond_dev_t          → ble_gap_sec_keyset_t
ESP_BLE_SEC_ENCRYPT_MITM    → BLE_GAP_SEC_MODE_SC_MITM
ESP_IO_CAP_OUT               → BLE_GAP_IO_CAPS_DISPLAY_ONLY
sd_ble_gatts_mtu_set()       → ble_enable()
```

---

### 5.3 Porting to Desktop/Simulator

**Difficulty**: Medium | **Timeline**: 2-4 weeks

#### Architecture

```
┌─────────────────────────────────────────────────┐
│              Desktop Application                 │
│  ┌──────────────┐  ┌──────────────────────────┐  │
│  │ BLE Dongle   │  │ Display (native window) │  │
│  │ (BlueZ/WinRT)│  │ (SDL2, GLFW, Qt, etc.)   │  │
│  └──────────────┘  └──────────────────────────┘  │
└─────────────────────────────────────────────────┘
```

#### Components to Implement

1. **BLE Client** (acting as Claude desktop app):
   - Use platform BLE APIs (BlueZ on Linux, WinRT on Windows, CoreBluetooth on macOS)
   - Connect to physical buddy device for testing
   - Or: Implement mock mode that simulates desktop app

2. **Display**:
   - Replace M5StickCPlus TFT with native graphics
   - Keep same pixel dimensions (135x240) or scale
   - Use sprite-based double buffering matching current approach

3. **Input**:
   - Map keyboard/mouse to button events
   - Implement tap vs long-press detection

4. **Storage**:
   - Replace NVS with file-based JSON or SQLite
   - Replace LittleFS with host file system

#### Mock Mode
For testing without hardware, implement a mock that:
- Simulates session state changes
- Shows the UI on desktop
- Responds to button clicks

---

### 5.4 Porting to Web (Web Bluetooth)

**Difficulty**: High | **Timeline**: 4-8 weeks

#### Architecture

```
┌─────────────────────────────────────────────────┐
│                 Web Browser                      │
│  ┌──────────────────┐  ┌──────────────────────┐  │
│  │ Web Bluetooth    │  │ Canvas/WebGL Display │  │
│  │ (Chrome/Edge)     │  │                      │  │
│  └──────────────────┘  └──────────────────────┘  │
└─────────────────────────────────────────────────┘
```

#### Key Challenges

1. **Web Bluetooth Limitations**:
   - Only Chrome/Edge support Web Bluetooth
   - Limited to GATT clients (device must be peripheral)
   - No LE Secure Connections passkey support in browser

2. **Security Model Mismatch**:
   - Web Bluetooth cannot perform passkey entry
   - Bonding not supported in browser context

3. **File System**:
   - Use IndexedDB or File System Access API
   - GIFs would need to be embedded or fetched

#### Workaround Options
- Use Chrome flags for experimental Web Bluetooth features
- Implement companion app (Electron/Tauri) as bridge
- Target PWA with native BLE via plugin

---

## 6. State Machine Reference

### 6.1 Persona States (7 states)

| State | Trigger | Display |
|-------|---------|---------|
| `P_SLEEP` | `!connected` or face-down | Zzz animation, dim |
| `P_IDLE` | Connected, 0 sessions | Normal idle animation |
| `P_BUSY` | `sessionsRunning >= 3` | Excited animation |
| `P_ATTENTION` | `sessionsWaiting > 0` | Alert/attention animation |
| `P_CELEBRATE` | `recentlyCompleted` | Celebration animation |
| `P_DIZZY` | Shake detected | Dazed/spinning animation |
| `P_HEART` | Fast approval (<5s) | Heart/love animation |

### 6.2 State Derivation Logic

```cpp
PersonaState derive(const TamaState& s) {
  if (!s.connected)            return P_IDLE;
  if (s.sessionsWaiting > 0)   return P_ATTENTION;
  if (s.recentlyCompleted)     return P_CELEBRATE;
  if (s.sessionsRunning >= 3)  return P_BUSY;
  return P_IDLE;
}
```

### 6.3 Display Modes

| Mode | Content |
|------|---------|
| `DISP_NORMAL` | Character + HUD transcript |
| `DISP_PET` | Pet stats/how-to pages |
| `DISP_INFO` | Device info pages (6 pages) |

---

## 7. Memory Considerations

### 7.1 ESP32 Memory Layout

| Region | Size | Usage |
|--------|------|-------|
| Flash | 4MB | Firmware + LittleFS |
| SRAM | ~80KB heap | Runtime allocations |
| GIF buffer | ~20KB | AnimatedGIF frame buffer |
| Sprite | 135×240×2 = ~64KB | Display sprite |

### 7.2 Porting Memory Budget

When porting to constrained devices:
- **Minimum RAM**: 64KB for sprite + 20KB for GIF decode
- **Stack**: Main loop uses ~2KB stack
- **Heap**: Dynamic allocations in GIF decode, JSON parsing

### 7.3 Optimization Strategies

1. **Reduce sprite size**: Scale down display dimensions
2. **Stream GIF decoding**: Decode one frame at a time
3. **Static allocation**: Avoid malloc in main loop
4. **JSON pooling**: Reuse JSON document buffers

---

## 8. Build System

### 8.1 PlatformIO Commands

```bash
pio run              # Build firmware
pio run -t upload    # Flash firmware via USB
pio run -t uploadfs  # Flash LittleFS filesystem
pio run -t erase     # Wipe device
pio run -t clean     # Clean build artifacts
pio run --environment m5stickc-plus  # Build specific env
```

### 8.2 Build Flags

```ini
build_flags =
    -DCORE_DEBUG_LEVEL=0    # Disable serial debug
board_build.filesystem = littlefs
board_build.partitions = no_ota.csv   # No OTA, single partition
board_build.f_cpu = 160000000L        # 160MHz CPU
build_src_filter = +<*> +<buddies/>   # Include buddies/ dir
```

### 8.3 Library Dependencies

```ini
lib_deps =
    m5stack/M5StickCPlus       # Hardware abstraction
    bitbank2/AnimatedGIF@^2.1.1 # GIF decoding
    bblanchon/ArduinoJson@^7.0.0 # JSON parsing
```

---

## 9. Testing

### 9.1 BLE Testing

1. **nRF Connect** (mobile): Scan for `Claude-XXXX`, connect, view services
2. **BlueZ `bleshell`** (Linux): Interactive GATT exploration
3. **Desktop Hardware Buddy**: Full integration test with Claude app

### 9.2 Mock Testing

Create a mock BLE peripheral:
- Python: `pybluez` or ` bleak` (Linux/macOS/Windows)
- Simulates the desktop app side
- Test protocol compliance

### 9.3 Protocol Testing

Verify JSON message parsing:
- Heartbeat snapshot parsing
- Permission decision encoding
- Status response formatting
- Folder push protocol

---

## 10. Contributing Changes Back

From `CONTRIBUTING.md`:

> "The protocol is the stable surface — REFERENCE.md is the contract, this firmware is just one way to honor it."

If porting to a new platform:

1. **Document your implementation** with clear setup instructions
2. **Maintain protocol compliance** — REFERENCE.md is the source of truth
3. **Consider contributing** to the GitHub repository via fork
4. **Test with Claude desktop apps** in Developer Mode

---

## 11. Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-04-27 | Claude | Initial draft |

---

## Appendix A: Key Constants

```cpp
// Display
const int W = 135, H = 240;
const int CX = W / 2;
const int CY_BASE = 120;

// LED
const int LED_PIN = 10;  // active-low

// Timing
const uint32_t SCREEN_OFF_MS = 30000;
const uint32_t WAKE_TRANSITION_MS = 12000;

// BLE
static const size_t RX_CAP = 2048;
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// IMU Thresholds
#define FACE_DOWN_AZ_THRESHOLD -0.7f
#define FACE_DOWN_AX_AY_MAX    0.4f
#define SHAKE_DELTA_THRESHOLD  0.8f
```

---

## Appendix B: File Manifest

```
claude-desktop-buddy/
├── src/
│   ├── main.cpp           # 1265 lines - main loop, state machine
│   ├── ble_bridge.cpp     # 180 lines - BLE server
│   ├── ble_bridge.h       # 18 lines - BLE interface
│   ├── data.h             # ~200 lines - JSON, session state
│   ├── buddy.cpp/h        # ASCII pet dispatch
│   ├── buddies/           # 18 species files
│   ├── character.cpp/h    # GIF rendering
│   ├── stats.h            # NVS persistence
│   └── xfer.h             # folder push protocol
├── characters/
│   └── bufo/              # Example character pack
├── docs/
│   ├── manual.html
│   └── screenshots/
├── tools/
│   ├── prep_character.py
│   ├── flash_character.py
│   ├── test_serial.py
│   └── test_xfer.py
├── platformio.ini
├── README.md
├── REFERENCE.md           # BLE protocol spec
└── CONTRIBUTING.md
```
