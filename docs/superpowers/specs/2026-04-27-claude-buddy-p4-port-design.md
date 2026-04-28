# Claude Desktop Buddy: ESP32-P4 Port Design

> **Status**: Approved Design (updated 2026-04-28 — BLE unblocked via esp_hosted managed components)
> **Target Hardware**: Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3
> **Source Project**: claude-desktop-buddy (M5StickC Plus / Arduino / PlatformIO)
> **Date**: 2026-04-27

---

## 1. Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      ESP32-P4 (Main CPU)                         │
│                                                                  │
│  ┌─────────────────────────┐  ┌──────────────────────────────┐  │
│  │ LVGL UI (480×800 MIPI)   │  │ BLE Bridge / NimBLE NUS      │  │
│  │ • Pet/GIF animation      │  │ (talks to C6 via SDIO)       │  │
│  │ • Permission UI          │  ├──────────────────────────────┤  │
│  │ • Clock, Settings        │  │ Data Layer (data.h port)     │  │
│  │ • Touch controls         │  │ JSON parsing, state machine  │  │
│  └─────────────────────────┘  ├──────────────────────────────┤  │
│                                │ Character/GIF Renderer       │  │
│                                │ (GIF → LVGL canvas)          │  │
│                                ├──────────────────────────────┤  │
│                                │ Persistence                  │  │
│                                │ • nvs_flash (settings/stats) │  │
│                                │ • SD card (characters/GIFs)  │  │
│                                ├──────────────────────────────┤  │
│                                │ Audio (ES8311 beeps)         │  │
│  ┌─────────────────────────┐  └──────────────────────────────┘  │
│  │ SDIO link to C6          │                                    │
│  │ (esp_hosted transport)    │                                    │
│  └───────────┬─────────────┘                                    │
└──────────────┼──────────────────────────────────────────────────┘
               │ SDIO (4-bit, 40MHz)
┌──────────────▼──────────────────────────────────────────────────┐
│                      ESP32-C6 (Coprocessor)                      │
│                                                                  │
│  ESP-Hosted Slave Firmware (pre-flashed from factory)            │
│  • Full WiFi 6 + BLE 5 stack                                     │
│  • Hosted HCI: HCI commands/events multiplexed over SDIO         │
│  • Nordic UART Service (6e400001...) runs in P4's NimBLE host    │
│  • Raw BLE data relayed to P4 via SDIO Hosted HCI                │
└──────────────────────────────────────────────────────────────────┘
```

### Key Architectural Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Framework | **ESP-IDF 6.0** | Arduino unsupported on ESP32-P4 |
| Display | **LVGL v9 via Waveshare BSP** | Hardware-accelerated, touch support, standard examples |
| LVGL Adapter | **Direct LVGL init** (bypassed esp_lvgl_adapter) | Adapter v0.1.x incompatible with IDF v6.0 |
| BLE | **esp_hosted v2 + esp_wifi_remote via managed components** | C6 runs ESP-Hosted slave firmware (pre-flashed). P4 runs NimBLE host + Hosted HCI over shared SDIO. Pattern from `esp-hosted-mcu/examples/host_nimble_bleprph_host_only_vhci` |
| Input | **Capacitive touch** | Replaces button navigation, more intuitive |
| Audio | **ES8311 via I2S codec component** | For beeps/tones (deferred — not yet adapted for IDF v6.0) |
| Storage | **SD card (SDMMC) + nvs_flash** | SD for large GIF files, NVS for settings |
| IMU | **Skipped** | No onboard IMU; nap replaced by idle timeout |
| CPU Freq | **360 MHz** (P4 v1.3 chip, CPLL) | 400 MHz not supported on pre-v3 revisions |

---

## 2. Component Map

### Reuse Portable Components (unchanged core logic)

| Source File | Changes Needed |
|-------------|----------------|
| `data.h` | Remove `M5.Rtc` refs, replace with time_t; remove `M5.Imu` dep; replace `Serial`→`uart`; replace ArduinoJson→cJSON |
| `xfer.h` | Remove `M5StickCPlus.h` include; replace `LittleFS`→`SD` or `FAT`; replace `Preferences`→`nvs_flash` |
| `buddy.cpp` | No changes — renders to any uint16_t* buffer |
| `buddies/*.cpp` | No changes — 18 ASCII species |
| `buddy_common.h` | No changes — shared constants |
| `ble_bridge.h` | Keep interface, implementation TBD (C6 vs hosted) |

### New/Replaced Components

| Component | Source | Description |
|-----------|--------|-------------|
| `main.c` | New | ESP-IDF `app_main`, FreeRTOS tasks, LVGL setup, BLE event loop |
| `ble_bridge.c` | Replace | BLE NUS implementation — approach TBD (custom C6 firmware or ported esp_hosted) |
| `display.c` | New | LVGL screen manager: pet area, HUD, approval, clock, settings, info |
| `character.c` | Port | GIF decode → LVGL canvas (swap AnimatedGIF for LVGL GIF or keep lib) |
| `stats.c` | Port | Settings/stats persistence via nvs_flash instead of Preferences |
| `touch.c` | New | Touch gesture handler: tap, long-press, swipe |
| `audio.c` | New | ES8311 beep/tone driver (deferred to Phase 4) |
| `fs_storage.c` | New | SD card init + file ops for characters |

---

## 3. UI Layout (480×800)

### Normal Mode (Default)
```
┌─────────────────────────────────┐  0
│  Status bar                      │  30
│  [Claude-XXXX] [🔒] [3/1/1]     │
├─────────────────────────────────┤
│                                 │
│   Pet / Character area           │
│   (centered, full-color)         │  30-480
│   • GIF animations               │
│   • ASCII pet rendering          │
│   • State animations             │
│                                 │
├─────────────────────────────────┤ 480
│  HUD / Transcript (scrollable)   │  480-670
│  • Session transcript lines      │
│  • Token counts                  │
│  • Session totals                │
│                                 │
├─────────────────────────────────┤ 670
│  Touch navigation bar            │  670-700
│  [◀] [Clock] [Pet] [Info] [▶]   │
├─────────────────────────────────┤ 700
│  Approval panel (conditional)    │  700-800
│  [Approve ✓]     [Deny ✗]       │
│  Tool: Bash   waited: 12s       │
│  rm -rf /tmp/foo                │
└─────────────────────────────────┘ 800
```

### Other Screens

- **Clock**: Full-screen digital clock + date + weekday + small pet in corner
- **Pet Stats**: Mood (hearts), fed (dots), energy (bars), level, approval/deny counts, tokens
- **Info**: 6 pages — About, Controls, Claude stats, Device info, Bluetooth, Credits
- **Settings**: Touch slider (brightness), toggle switches (sound, BT, LED, HUD, clock rot)
- **Menu**: Settings, turn off, help, about, demo, close

---

## 4. Data Flow

### BLE Inbound
```
Desktop BLE → C6 BLE radio
  → C6 firmware encodes HCI event → SDIO
  → esp_hosted transport receives Hosted HCI frame
  → NimBLE host stack on P4 processes HCI event
  → data.h accumulates bytes in ring buffer
  → dataPoll() parses \n-delimited JSON
  → TamaState updated
  → derive() computes PersonaState
  → LVGL UI updates on next tick
```

### Permission Outbound
```
Touch approve/deny
  → main.c formats {"cmd":"permission","id":"...","decision":"..."}
  → NimBLE NUS TX notify → Hosted HCI → SDIO → C6 → BLE notify → desktop
```

### Folder Push
```
Desktop drops folder
  → C6 BLE → Hosted HCI → SDIO → NimBLE host → xfer.h parses commands
  → Writes files to SD card /characters/<name>/
  → characterInit() loads manifest.json
  → LVGL pet canvas starts animating
```

---

## 5. State Machine

Identical to original — 7 persona states:

| State | Trigger | Display |
|-------|---------|---------|
| `P_SLEEP` | !connected or idle timeout | Dim screen, Zzz animation |
| `P_IDLE` | Connected, 0 sessions | Normal idle |
| `P_BUSY` | >= 3 sessions running | Excited animation |
| `P_ATTENTION` | Sessions waiting | Alert/attention |
| `P_CELEBRATE` | Recently completed | Confetti/celebration |
| `P_DIZZY` | Shake (removed — unused) | Dazed/spinning |
| `P_HEART` | Fast approval <5s | Hearts/love |

Removed: shake detection (no IMU), face-down nap (replaced by idle timeout).

---

## 6. BLE Protocol (NUS on C6)

Identical UUIDs and protocol as REFERENCE.md:
- Service: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX (write): `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- TX (notify): `6e400003-b5a3-f393-e0a9-e50e24dcca9e`
- LE Secure Connections with passkey entry (DisplayOnly)
- Advertise as `Claude-XXXX`

Implementation approach:
- **Hosted HCI (VHCI)** over shared SDIO transport — no extra GPIOs needed
- P4 runs NimBLE host stack with NUS GATT service
- C6 runs ESP-Hosted slave firmware (pre-flashed from factory, no reflash needed)
- `esp_hosted` bt_controller_init/enable brings C6 BT controller online
- Pattern from `esp-hosted-mcu/examples/host_nimble_bleprph_host_only_vhci/`

---

## 7. Memory & Performance

| Resource | Available | Expected Usage |
|----------|-----------|----------------|
| PSRAM | 32MB | LVGL buffers (~2MB), GIF frames (~200KB), heap |
| Flash | 32MB NOR | Firmware + settings |
| SD Card | User-provided | Character files (GIFs, manifests) |
| CPU | RISC-V 360MHz dual-core | LVGL GPU via PPA, BLE on C6 |

No memory concerns — the P4 has orders of magnitude more RAM than the original ESP32.

---

## 8. Phased Implementation

### Phase 1: Foundation ⏸ (blocked on C6 firmware)
1. ✅ ESP-IDF project with correct CMakeLists, sdkconfig, partitions
2. ✅ Display + touch init via Waveshare BSP, LVGL "Hello World" — **adapted for IDF v6.0**
3. ✅ C6 SDIO link — `esp_hosted_connect_to_slave()` works. Managed components fetched.
4. ❌ C6 BT controller init — `esp_hosted_bt_controller_init()` fails: RPC protocol version mismatch (host 2.12.0 vs C6 firmware 0.0.0). C6 capabilities (`0xd`) confirm HCI+BLE supported — needs firmware update to match host RPC protocol.
5. ⏸ BLE NUS service — Code written (ble_nus.c/h), blocked on #4
6. ❌ Port `data.h` — deferred
7. ❌ Display "Connected / Idle" status — deferred
8. ❌ Idle timeout → screen dimming — deferred

### Phase 2: State Machine + Interaction
1. Persona state machine (7 states)
2. LVGL pet display area with state transitions
3. Permission prompt UI with touch approve/deny buttons
4. BLE status response generation
5. Time sync → internal time display
6. Touch gesture handling (tap, long-press, swipe)
7. Display mode navigation (Normal, Clock, Pet, Info)

### Phase 3: Characters + Settings
1. GIF decode — LVGL GIF widget or AnimatedGIF library → LVGL canvas
2. Manifest.json parsing (reuse original format)
3. ASCII pet rendering into LVGL (port buddy.cpp)
4. Settings panel UI (brightness slider, toggles)
5. Info screens (About, Controls, Claude, Device, Bluetooth, Credits)
6. Character cycling (GIF ↔ ASCII species)

### Phase 4: Polish
1. Audio beeps via ES8311
2. Clock screen with full-date display
3. Screen saver / ambient animation mode
4. Folder push protocol (port xfer.h)
5. Stats persistence (nvs_flash)
6. Touch gesture refinement
7. Performance tuning

---

## 9. Ported vs Replaced Components

### Portable (minimal changes)
- `data.h` — JSON parsing, TamaState, line accumulation
- `buddy.cpp/h` — ASCII pet dispatch
- `buddies/*.cpp` — 18 species (one per file)
- `buddy_common.h` — shared constants
- `ble_bridge.h` — interface (implementation TBD)

### Rewritten
- `main.cpp` → `main.c` — ESP-IDF app_main, FreeRTOS tasks
- `ble_bridge.cpp` — BLE implementation TBD (custom C6 firmware or ported esp_hosted)
- `character.cpp` — GIF decode to LVGL canvas (different rendering pipeline)
- `stats.h` — Preferences → nvs_flash
- `xfer.h` — LittleFS → SD card

### New
- `display.c` — LVGL screen management
- `touch.c` — Gesture detection
- `audio.c` — ES8311 beeps (deferred)
- `fs_storage.c` — SD card file operations

---

## 10. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| SDIO pin mapping differs between Waveshare board and ESP32-P4-Function-EV-Board | **Medium** | Waveshare WiFi example works with esp_hosted, proving pins are correct. If mismatch, configure via Kconfig or `esp_hosted_set_sdio_pins()` |
| MIPI DSI + LVGL complexity | Low | BSP component handles low-level; display now working |
| GIF decode performance | Low | 32MB PSRAM + PPA acceleration; can pre-decode frames |
| Audio driver (ES8311) not yet adapted for IDF v6.0 | Low | Deferred to Phase 4; esp_codec_dev needs update |
| Touch gesture robustness | Low | Simple tap/long-press/swipe; refine iteratively |
| IDF v6.0 managed component version compatibility | **Medium** | Pin to known-working versions from IDF v6.0 examples (`esp_hosted ~2`, `esp_wifi_remote ~0.14`) |
