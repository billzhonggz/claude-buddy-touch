# Claude Desktop Buddy: ESP32-P4 Port Design

> **Status**: Approved Design (updated 2026-05-02 — Phase 1 complete, BLE advertising & connected)
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
│  │ • ASCII buddy animation  │  │ (talks to C6 via SDIO)       │  │
│  │ • Session status display │  ├──────────────────────────────┤  │
│  │ • Demo mode (scenarios)  │  │ Data Layer (data.h ✓ ported) │  │
│  │ • Touch controls         │  │ cJSON parsing, TamaState     │  │
│  └─────────────────────────┘  │ demo mode, BLE line buffer    │  │
│                                ├──────────────────────────────┤  │
│                                │ State Machine                 │  │
│                                │ derive_state() — 5 states    │  │
│                                ├──────────────────────────────┤  │
│                                │ Persistence                   │  │
│                                │ • nvs_flash (initialized)     │  │
│                                │ • SD card (HW ready, BSP)     │  │
│                                ├──────────────────────────────┤  │
│                                │ Audio (ES8311, deferred)      │  │
│  ┌─────────────────────────┐  └──────────────────────────────┘  │
│  │ SDIO link to C6          │                                    │
│  │ (esp_hosted transport)    │                                    │
│  └───────────┬─────────────┘                                    │
└──────────────┼──────────────────────────────────────────────────┘
               │ SDIO (4-bit, 40MHz)
┌──────────────▼──────────────────────────────────────────────────┐
│                      ESP32-C6 (Coprocessor)                      │
│                                                                  │
│  ESP-Hosted Slave Firmware v2.12.6 (reflashed from factory)     │
│  • Full WiFi 6 + BLE 5 stack                                     │
│  • Hosted HCI: HCI commands/events multiplexed over SDIO         │
│  • Nordic UART Service (6e400001...) runs in P4's NimBLE host    │
│  • Raw BLE data relayed to P4 via SDIO Hosted HCI                │
└──────────────────────────────────────────────────────────────────┘
```

### Key Architectural Decisions (updated)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Framework | **ESP-IDF 6.0** | Arduino unsupported on ESP32-P4 |
| Display | **LVGL v9 via Waveshare BSP** | Hardware-accelerated, touch support |
| LVGL Init | **Direct LVGL init** | `esp_lvgl_adapter` incompatible with IDF v6.0 |
| BLE | **esp_hosted v2 + esp_wifi_remote via managed components** | C6 runs ESP-Hosted slave FW v2.12.6. P4 runs NimBLE host-only via Hosted HCI VHCI over shared SDIO. Verified working: advertising, connections, NUS RX/TX. |
| Input | **Capacitive touch (GT911)** | Replaces buttons — tap, long-press implemented |
| Audio | **ES8311 via I2S** | For beeps/tones (deferred to Phase 4) |
| Storage | **SD card (SDMMC) + nvs_flash** | SD for large GIF files (HW ready via BSP), NVS for settings |
| IMU | **Skipped** | No onboard IMU; idle timeout replaces face-down nap |
| CPU Freq | **360 MHz** (P4 v1.3, CPLL) | 400 MHz not supported on pre-v3 revisions |
| JSON | **cJSON (managed component)** | Replaced ArduinoJson for IDF compatibility |

---

## 2. Component Map

### Reusable (ported from original, minimal changes)

| Source File | Status | Notes |
|-------------|--------|-------|
| `data.h` | ✅ Ported | ArduinoJson → cJSON, removed M5 deps, added demo mode |
| `data.h` line buf | ✅ Ported | BLE byte-stream → `\n`-delimited JSON line buffer |
| `data.h` TamaState | ✅ Ported | Session tracking, time sync, prompt data |
| `buddy.cpp/h` | ⏸ Not yet | ASCII pet dispatch — deferred to Phase 3 |
| `buddies/*.cpp` | ⏸ Not yet | 18 ASCII species — deferred to Phase 3 |
| `buddy_common.h` | ⏸ Not yet | Shared constants — deferred to Phase 3 |

### New/Replaced (ESP-IDF native)

| Component | Status | Description |
|-----------|--------|-------------|
| `main.c` | ✅ Done | ESP-IDF `app_main`, FreeRTOS task, state machine, demo loop |
| `display.c/h` | ✅ Done | LVGL screen: status bar, ASCII buddy area, session labels |
| `data.h` (header-only) | ✅ Done | JSON parsing, TamaState, demo mode, BLE feed buffer, idle timeout |
| `touch.c/h` | ✅ Done | GT911 gesture: tap (advance demo), long-press (toggle demo) |
| `ble_nus.c/h` | ✅ Done | BLE NUS advertising `Claude-XXXX`, connection callbacks, RX feed |
| `tama_state.h` | ✅ Done | TamaState struct definition (shared across modules) |
| `character.c` | ⏸ Not started | GIF decode → LVGL canvas (Phase 3) |
| `stats.c` | ⏸ Not started | nvs_flash persistence for settings (Phase 4) |
| `audio.c` | ⏸ Not started | ES8311 beeps (Phase 4) |
| `fs_storage.c` | ⏸ Not started | SD card file operations (Phase 3) |

---

## 3. UI Layout (480×800)

### Normal Mode (Current Implementation)

```
┌─────────────────────────────────┐  0
│  Status bar                      │  30
│  [Claude-XXXX] [●Connected]      │
│  Sessions: 3/1/1  Tokens: 89k   │
├─────────────────────────────────┤
│                                 │
│   ASCII buddy area               │
│   (large capybara-style art)     │  30-480
│   • 5 persona expressions        │
│   • Scaled to fit                │
│   • State color bar              │
│                                 │
├─────────────────────────────────┤ 480
│  HUD / Status line               │  480-500
│  • Current state name            │
│  • Session: active/completed     │
│  • Demo mode indicator           │
│                                 │
├─────────────────────────────────┤ 500-800
│  (reserved for future content)   │
│  • Transcript (scrollable)       │
│  • Touch nav bar                 │
│  • Approval panel                │
└─────────────────────────────────┘ 800
```

### Future Screens (Phases 2-4)

- **Clock**: Full-screen digital clock + date + weekday (Phase 2)
- **Pet Stats**: Mood, fed, energy, approval/deny counts, tokens (Phase 3)
- **Info**: 6 pages — About, Controls, Claude stats, Device info, Bluetooth, Credits (Phase 3)
- **Settings**: Touch slider (brightness), toggles (sound, BT, LED, HUD, clock rot) (Phase 3)
- **Approval panel**: Tool prompt with Approve/Deny buttons (Phase 2)
- **Transcript**: Scrollable session transcript lines (Phase 2)

---

## 4. Data Flow

### BLE Inbound (Verified Working)
```
Desktop BLE → C6 BLE radio (FW v2.12.6)
  → C6 firmware encodes HCI event → SDIO (4-bit, 40MHz)
  → esp_hosted transport receives Hosted HCI frame
  → NimBLE host stack on P4 processes HCI event
  → ble_nus_rx_handler() → on_ble_rx() callback
  → data.h accumulates bytes in line buffer
  → data_poll() → data_connected() checks 30s keepalive
  → TamaState updated
  → derive_state() computes PersonaState
  → display_update() renders on next LVGL tick
```

### Permission Outbound (Planned — Phase 2)
```
Touch approve/deny
  → main.c formats {"cmd":"permission","id":"...","decision":"..."}
  → NimBLE NUS TX notify → Hosted HCI → SDIO → C6 → BLE notify → desktop
```

### Folder Push (Planned — Phase 4)
```
Desktop drops folder
  → C6 BLE → Hosted HCI → SDIO → NimBLE host → xfer.h parses commands
  → Writes files to SD card /characters/<name>/
  → characterInit() loads manifest.json
  → LVGL pet canvas starts animating
```

---

## 5. State Machine

Derived from original 7 states, trimmed to 5 (removed P_DIZZY — no IMU/shake — and P_HEART deferred):

| State | Trigger | Current Display |
|-------|---------|----------------|
| `P_SLEEP` | Not connected (>30s timeout) | Sleep ASCII art, "No Claude connected" |
| `P_IDLE` | Connected, 0 sessions waiting | Idle ASCII art, session count |
| `P_BUSY` | >= 3 sessions running | Busy ASCII art, high token count |
| `P_ATTENTION` | Sessions waiting (>0) | Alert ASCII art, sessions waiting |
| `P_CELEBRATE` | recentlyCompleted flag | Celebratory ASCII art |

**Note:** P_HEART (fast approvals <5s) is deferred to Phase 2 refinement.

---

## 6. BLE Protocol (NUS via Hosted HCI — Verified Working)

Identical UUIDs and protocol as [`docs/REFERENCE.md`](../../REFERENCE.md):
- Service: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX (write, desktop→device): `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- TX (notify, device→desktop): `6e400003-b5a3-f393-e0a9-e50e24dcca9e`
- LE Secure Connections with passkey entry
- Advertises as `Claude-XXXX`

### Implementation

| Layer | Component | Status |
|-------|-----------|--------|
| SDIO transport | `esp_hosted` managed component | ✅ Verified — `connect_to_slave()` succeeds |
| C6 firmware | ESP-Hosted slave FW v2.12.6 | ✅ **Reflashed** from factory v0.0.6 — RPC protocol mismatch resolved |
| BT controller init | `esp_hosted_bt_controller_init/enable` | ✅ Verified — no errors |
| BLE host stack | NimBLE (host-only via VHCI) | ✅ Working — gatts register, advertising |
| NUS service | `ble_nus.c/h` | ✅ Verified — advertising, connections, RX callbacks |
| Advertising data | 31-byte limit workaround | ✅ 128-bit NUS UUID in scan response, not adv data |

### Lessons Learned (Brings-up)

1. **`ble_gatts_count_cfg rc=3`** — NimBLE requires non-NULL access callbacks on all characteristics. Fixed by providing a shared access callback for all NUS characteristics.
2. **`ble_gap_adv_set_fields rc=4`** — Advertisement data exceeded 31-byte limit. Fixed by moving 128-bit NUS service UUID to scan response.

---

## 7. Memory & Performance

| Resource | Available | Measured Usage |
|----------|-----------|----------------|
| PSRAM | 32MB | LVGL buffers (~2MB), heap via malloc |
| Flash | 32MB NOR | Firmware ~1.2MB, remaining for SPIFFS storage |
| SD Card | User-provided | BSP SDMMC init verified — character storage (Phase 3) |
| CPU | RISC-V 360MHz dual-core | LVGL flush, app task at 20Hz, idle ~80% |

No memory concerns — P4 has orders of magnitude more RAM than original ESP32.

---

## 8. Phased Implementation (Updated)

### Phase 1: Foundation ✅ COMPLETE

| Task | Status | Notes |
|------|--------|-------|
| 1. Project skeleton | ✅ Done | CMakeLists, partitions, sdkconfig, idf_component.yml |
| 2. BSP component | ✅ Done | Waveshare BSP adapted for IDF v6.0 |
| 3. Display via LVGL | ✅ Done | 480×800 full-screen with ASCII buddy + status |
| 4. C6 SDIO link + BT init | ✅ Done | C6 reflashed to ESP-Hosted slave FW v2.12.6. SDIO connect + BT controller init verified. |
| 5. BLE NUS service | ✅ Done | Advertising as `Claude-XXXX`, connections accepted, RX data fed into data layer |
| 6. data.h port | ✅ Done | cJSON-based JSON parsing, TamaState, demo mode, BLE line buffer, 30s idle timeout |
| 7. State display | ✅ Done | Dynamic status bar: Connecting/Connected/Idle/Active with session counts |
| 8. Touch basic | ✅ Done | GT911 tap (advance demo), long-press (toggle demo) |
| 9. Integration | ✅ Done | State-driven app task, demo loop, BLE fallback (graceful if C6 offline) |

### Phase 2: State Machine + Interaction (NEXT)

| Task | Priority | Status |
|------|----------|--------|
| 1. Full persona state machine (7 states → add P_HEART) | High | ⏸ Not started (5/7 states exist in code) |
| 2. LVGL pet display area with state color transitions | High | ⏸ Not started (basic text-based states) |
| 3. Permission prompt UI with touch approve/deny buttons | High | ⏸ Not started |
| 4. BLE status response generation (TX notify) | High | ⏸ Not started |
| 5. Time sync → internal time display | Medium | ⏸ Not started (time parsing in data.h, no UI) |
| 6. Touch gesture refinement (swipe, zones) | Medium | ⏸ Not started |
| 7. Display mode navigation (Normal, Clock, Pet, Info) | Medium | ⏸ Not started |
| 8. Transcript display (scrollable session lines) | Low | ⏸ Not started (lines parsed in data.h, no UI) |

### Phase 3: Characters + Settings

| Task | Priority | Status |
|------|----------|--------|
| 1. GIF decode → LVGL canvas | High | ⏸ Not started |
| 2. Manifest.json parsing | High | ⏸ Not started |
| 3. ASCII pet rendering (port buddy.cpp + 18 species) | High | ⏸ Not started |
| 4. Settings panel UI (brightness slider, toggles) | Medium | ⏸ Not started |
| 5. Info screens (About, Controls, Claude, Device, BT, Credits) | Low | ⏸ Not started |
| 6. Character cycling (GIF ↔ ASCII species) | Low | ⏸ Not started |

### Phase 4: Polish

| Task | Priority | Status |
|------|----------|--------|
| 1. Audio beeps via ES8311 | Low | ⏸ Not started |
| 2. Clock screen with full-date display | Low | ⏸ Not started |
| 3. Screen saver / ambient animation mode | Low | ⏸ Not started |
| 4. Folder push protocol (port xfer.h) | Low | ⏸ Not started |
| 5. Stats persistence (nvs_flash) | Low | ⏸ Not started |
| 6. Touch gesture refinement | Low | ⏸ Not started |
| 7. Performance tuning | Low | ⏸ Not started |

---

## 9. Ported vs Replaced Components

### Portable (minimal changes — from original project)
- `data.h` — ✅ Ported (cJSON, TamaState, demo mode, BLE buffer)
- `buddy.cpp/h` — ⏸ Pending (Phase 3)
- `buddies/*.cpp` — ⏸ Pending (18 species, Phase 3)
- `buddy_common.h` — ⏸ Pending (Phase 3)
- `ble_bridge.h` — ⏸ Pending interface definition (Phase 2)

### Rewritten
- `main.cpp` → `main.c` — ✅ Done (ESP-IDF app_main, FreeRTOS task)
- `ble_bridge.cpp` → `ble_nus.c/h` — ✅ Done (esp_hosted + NimBLE NUS)
- `character.cpp` → GIF decode to LVGL canvas — ⏸ Phase 3
- `stats.h` → nvs_flash persistence — ⏸ Phase 4
- `xfer.h` → SD card file ops — ⏸ Phase 4

### New
- `display.c/h` — ✅ Done (LVGL screen management)
- `touch.c/h` — ✅ Done (GT911 gesture detection)
- `audio.c` — ⏸ Phase 4 (ES8311 beeps)
- `fs_storage.c` — ⏸ Phase 3 (SD card file operations)
- `tama_state.h` — ✅ Done (shared TamaState struct)

---

## 10. IDF v6.0 Adaptations (Reference)

Record of changes needed for the BSP to work with IDF v6.0 (transferred from PROGRESS.md):

| Change | Reason |
|--------|--------|
| `driver` → `esp_driver_gpio`, `esp_driver_ledc`, etc. | Driver components split in v6.0 |
| Removed `esp_lvgl_adapter` | API incompatibility |
| Direct LVGL init (`lv_display_create`, `lv_indev_create`) | Replacement for adapter |
| `pixel_format` → `in_color_format`/`out_color_format` | DPI config API rename |
| `LCD_COLOR_PIXEL_FORMAT_RGB565` → `LCD_COLOR_FMT_RGB565` | Enum rename |
| Removed `use_dma2d` field | Field removed from DPI config struct |
| `ESP_LCD_COLOR_SPACE_RGB` → `LCD_RGB_ELEMENT_ORDER_RGB` | Enum rename |
| `MIPI_DSI_PHY_CLK_SRC_DEFAULT` → `MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M` | Clock source rename |
| Removed USB code (`usb/usb_host.h`) | USB component removed in v6.0 |
| Removed audio/I2S code (`esp_codec_dev`) | Incompatible with v6.0, not needed yet |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360` | P4 v1.3 uses CPLL 360MHz |
| Disabled task watchdog | LVGL init full-screen flush exceeds timeout |
| `SOC_WIRELESS_HOST_SUPPORTED=1` | P4 has no native WiFi/BT |
| `esp_phy` component is empty stub | P4 has no radio hardware |
| cJSON is managed component: `espressif/cjson` | Removed from base IDF |

---

## 11. Risks & Mitigations (Updated)

| Risk | Impact | Status |
|------|--------|--------|
| SDIO pin mapping mismatch | Medium | ✅ Resolved — Waveshare WiFi example verifies correct pins |
| C6 firmware RPC mismatch | Medium | ✅ **Resolved** — C6 reflashed to ESP-Hosted slave FW v2.12.6 (was factory v0.0.6) |
| MIPI DSI + LVGL complexity | Low | ✅ Resolved — display working with BSP + direct LVGL |
| GIF decode performance | Low | Pending — 32MB PSRAM available when implemented |
| Audio driver (ES8311) not adapted | Low | Deferred to Phase 4 |
| Touch gesture robustness | Low | Basic tap/long-press working; refinement in Phase 2 |
| IDF v6.0 managed component versions | Medium | ✅ Pinned to known-working versions |

---

## 12. Current File Structure

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
│   ├── PROGRESS.md                      # Implementation progress
│   ├── REFERENCE.md                     # BLE protocol reference
│   ├── PORTING.md                       # Platform porting guide
│   └── superpowers/
│       └── specs/
│           └── 2026-04-27-claude-buddy-p4-port-design.md
```
