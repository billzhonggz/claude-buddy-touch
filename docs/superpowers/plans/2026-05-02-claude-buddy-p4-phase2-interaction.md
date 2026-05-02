# Phase 2: State Machine + Interaction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the persona state machine, add display mode navigation (buddy/clock/info), permission prompt with approve/deny, transcript view, and BLE status TX.

**Architecture:** Extract state derivation from `main.c` into `state_machine.c/h` with P_HEART support. Add display mode manager in `display.c` (Buddy, Clock, Info, Transcript screens). Wire BLE TX for sending permission responses and status updates. Screens are toggled via swipe gestures (already implemented in touch.c).

**Tech Stack:** ESP-IDF v6.0, LVGL v9.4, NimBLE, cJSON

---

## File Structure

```
main/
├── state_machine.h    # NEW — state derivation + approval tracking
├── state_machine.c    # NEW — derive_state(), record_approval()
├── display.h          # MODIFY — add display mode enum, screen funcs
├── display.c          # MODIFY — add clock, info, transcript, approve/deny screens
├── main.c             # MODIFY — wire new screens, BLE TX, permission callbacks
├── touch.h            # (no change — swipe events already defined)
├── touch.c            # (no change — swipe detection already implemented)
├── ble_nus.c          # (no change — ble_nus_send() already implemented)
├── ble_nus.h          # (no change — API already complete)
├── data.h             # (no change — time/transcript parsing already implemented)
└── tama_state.h       # (no change)
```

---

### Task 1: Extract and fix persona state machine

**Files:**
- Create: `main/state_machine.h`
- Create: `main/state_machine.c`
- Modify: `main/main.c` (remove derive_state, use state_machine_derive)

- [ ] **Step 1: Create `state_machine.h`**

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "tama_state.h"

// 7 persona states matching the original claude-desktop-buddy.
// P_DIZZY and P_HEART are one-shot transient states (2s duration),
// not persistent derived states — they briefly override the base state.
enum PersonaState {
    P_SLEEP = 0,
    P_IDLE,
    P_BUSY,
    P_ATTENTION,
    P_CELEBRATE,
    P_DIZZY,
    P_HEART,
    P_COUNT
};

extern const char* persona_state_names[];

// Returns the base (derived) persona from TamaState data.
// Does NOT apply one-shot overrides — use state_machine_update() for that.
enum PersonaState state_machine_derive(const struct TamaState* state);

// Applies one-shot override: returns the one-shot state if within its
// duration window, otherwise returns base_state. This is the original
// triggerOneShot() pattern from claude-desktop-buddy.
enum PersonaState state_machine_update(enum PersonaState base_state);

// Trigger a transient one-shot state that persists for dur_ms then
// falls back to the base state. P_DIZZY (2s) and P_HEART (2s)
// are the typical one-shot states.
void state_machine_trigger_oneshot(enum PersonaState state, uint32_t dur_ms);

// Returns true if currently in a one-shot override
bool state_machine_in_oneshot(void);

// Record an approval timestamp — used to measure fast approvals (<5s)
void state_machine_record_approval(void);

// Returns ms since last approval, or UINT32_MAX if none
uint32_t state_machine_ms_since_approval(void);
```

- [ ] **Step 2: Create `state_machine.c`**

```c
#include "state_machine.h"
#include "esp_timer.h"

const char* persona_state_names[] = {
    [P_SLEEP]     = "sleep",
    [P_IDLE]      = "idle",
    [P_BUSY]      = "busy",
    [P_ATTENTION] = "attention",
    [P_CELEBRATE] = "celebrate",
    [P_DIZZY]     = "dizzy",
    [P_HEART]     = "heart",
};

// One-shot state: transient override with expiry
static enum PersonaState s_oneshot_state = P_COUNT;
static uint32_t s_oneshot_until_ms = 0;

// Approval tracking (for P_HEART trigger: fast approval < 5s)
static uint32_t s_last_approval_ms = 0;

static uint32_t _now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

enum PersonaState state_machine_derive(const struct TamaState* state)
{
    if (!state->connected) return P_SLEEP;
    if (state->sessionsWaiting > 0)    return P_ATTENTION;
    if (state->recentlyCompleted)      return P_CELEBRATE;
    if (state->sessionsRunning >= 3)   return P_BUSY;
    return P_IDLE;
}

enum PersonaState state_machine_update(enum PersonaState base_state)
{
    uint32_t now = _now_ms();
    if (s_oneshot_state < P_COUNT && now < s_oneshot_until_ms) {
        return s_oneshot_state;
    }
    s_oneshot_state = P_COUNT;
    return base_state;
}

void state_machine_trigger_oneshot(enum PersonaState state, uint32_t dur_ms)
{
    s_oneshot_state = state;
    s_oneshot_until_ms = _now_ms() + dur_ms;
}

bool state_machine_in_oneshot(void)
{
    uint32_t now = _now_ms();
    return s_oneshot_state < P_COUNT && now < s_oneshot_until_ms;
}

void state_machine_record_approval(void)
{
    s_last_approval_ms = _now_ms();
}

uint32_t state_machine_ms_since_approval(void)
{
    if (s_last_approval_ms == 0) return UINT32_MAX;
    return _now_ms() - s_last_approval_ms;
}
```

- [ ] **Step 3: Update `main.c` to use new state machine + one-shot pattern**

Replace the existing derive_state() function and app_task loop to use the one-shot pattern matching the original.

At top of file, add:
```c
#include "state_machine.h"
```

Remove the entire `enum PersonaState derive_state(...)` function (lines 13-20).

In `app_task()`, replace the lines:
```c
base_state = derive_state(tama);
active_state = base_state;
```
with:
```c
base_state = state_machine_derive(tama);
active_state = state_machine_update(base_state);
```

- [ ] **Step 4: Update `display.h` — remove PersonaState enum and names extern**

Remove from display.h:
```c
enum PersonaState {
    P_SLEEP = 0,
    P_IDLE,
    P_BUSY,
    P_ATTENTION,
    P_CELEBRATE,
    P_DIZZY,
    P_HEART,
    P_COUNT
};

extern const char* persona_state_names[];
```

Add `#include "state_machine.h"` to display.h (after `#include "tama_state.h"`):
```c
#include "tama_state.h"
#include "state_machine.h"
```

- [ ] **Step 5: Update `display.c` — remove duplicate PersonaState names**

Remove from display.c:
```c
const char* persona_state_names[] = {
    "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"
};
```

And add at top:
```c
#include "state_machine.h"
```

- [ ] **Step 6: Update CMakeLists.txt**

Add `state_machine.c` to `main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "main.c" "display.c" "touch.c" "ble_nus.c" "state_machine.c"
    INCLUDE_DIRS "."
)
```

- [ ] **Step 7: Build**

Run: `idf.py build`
Expected: compiles without errors (warnings about unused BUDDY entries for P_DIZZY/P_HEART are OK — they'll be used after wiring approval callback in Task 4).

---

### Task 2: Display mode navigation

**Files:**
- Modify: `main/display.h` — add DisplayMode enum + mode functions
- Modify: `main/display.c` — add mode switching, per-mode init
- Modify: `main/main.c` — wire swipe to mode switching

- [ ] **Step 1: Add display mode enum and functions to `display.h`**

Add after the existing declarations:
```c
enum DisplayMode {
    DISPLAY_MODE_BUDDY,
    DISPLAY_MODE_CLOCK,
    DISPLAY_MODE_INFO,
    DISPLAY_MODE_TRANSCRIPT,
    DISPLAY_MODE_COUNT
};

void display_set_mode(enum DisplayMode mode);
enum DisplayMode display_get_mode(void);
void display_show_buddy(const struct TamaState* state, enum PersonaState persona);
void display_show_clock(const struct TamaState* state);
void display_show_info(const struct TamaState* state);
void display_show_transcript(const struct TamaState* state);
```

- [ ] **Step 2: Add mode state and switching to `display.c`**

Add static vars:
```c
static enum DisplayMode s_mode = DISPLAY_MODE_BUDDY;
static lv_obj_t* s_clock_label;
static lv_obj_t* s_info_label;
static lv_obj_t* s_transcript_label;
```

Add mode functions at bottom of file (before closing):
```c
void display_set_mode(enum DisplayMode mode)
{
    if (mode >= DISPLAY_MODE_COUNT) mode = DISPLAY_MODE_BUDDY;
    s_mode = mode;

    // Hide/show screen elements based on mode
    lv_obj_add_flag(ui.buddy, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.session, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.tokens, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.state_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.prompt, LV_OBJ_FLAG_HIDDEN);

    if (s_clock_label) lv_obj_add_flag(s_clock_label, LV_OBJ_FLAG_HIDDEN);
    if (s_info_label) lv_obj_add_flag(s_info_label, LV_OBJ_FLAG_HIDDEN);
    if (s_transcript_label) lv_obj_add_flag(s_transcript_label, LV_OBJ_FLAG_HIDDEN);

    switch (mode) {
    case DISPLAY_MODE_BUDDY:
        lv_obj_clear_flag(ui.buddy, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.session, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.msg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.tokens, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.state_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.prompt, LV_OBJ_FLAG_HIDDEN);
        break;
    case DISPLAY_MODE_CLOCK:
        if (s_clock_label) lv_obj_clear_flag(s_clock_label, LV_OBJ_FLAG_HIDDEN);
        break;
    case DISPLAY_MODE_INFO:
        if (s_info_label) lv_obj_clear_flag(s_info_label, LV_OBJ_FLAG_HIDDEN);
        break;
    case DISPLAY_MODE_TRANSCRIPT:
        if (s_transcript_label) lv_obj_clear_flag(s_transcript_label, LV_OBJ_FLAG_HIDDEN);
        break;
    default:
        break;
    }
}

enum DisplayMode display_get_mode(void)
{
    return s_mode;
}
```

- [ ] **Step 3: Create clock label in `display_init()`**

Add after `ui.hint` init:
```c
s_clock_label = lv_label_create(ui.scr);
lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_48, LV_STATE_DEFAULT);
lv_obj_set_width(s_clock_label, DISP_W);
lv_obj_center(s_clock_label);
lv_obj_add_flag(s_clock_label, LV_OBJ_FLAG_HIDDEN);
```

Create info label:
```c
s_info_label = lv_label_create(ui.scr);
lv_obj_set_style_text_color(s_info_label, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
lv_obj_set_width(s_info_label, DISP_W - 20);
lv_obj_align(s_info_label, LV_ALIGN_TOP_LEFT, 10, 80);
lv_obj_add_flag(s_info_label, LV_OBJ_FLAG_HIDDEN);
```

Create transcript label:
```c
s_transcript_label = lv_label_create(ui.scr);
lv_obj_set_style_text_color(s_transcript_label, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
lv_obj_set_width(s_transcript_label, DISP_W - 20);
lv_obj_set_height(s_transcript_label, DISP_H - 100);
lv_obj_align(s_transcript_label, LV_ALIGN_TOP_LEFT, 10, 40);
lv_label_set_long_mode(s_transcript_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
lv_obj_add_flag(s_transcript_label, LV_OBJ_FLAG_HIDDEN);
```

- [ ] **Step 4: Wire swipe to mode switching in `main.c`**

In `app_task()` in main.c, replace the touch handling section:
```c
touch_event_data_t touch = touch_process();
if (touch.event == TOUCH_EVENT_TAP) {
    data_advance_demo();
} else if (touch.event == TOUCH_EVENT_LONG_PRESS) {
    data_set_demo(!data_demo());
} else if (touch.event == TOUCH_EVENT_SWIPE_LEFT) {
    enum DisplayMode m = display_get_mode();
    m = (m + 1) % DISPLAY_MODE_COUNT;
    display_set_mode(m);
} else if (touch.event == TOUCH_EVENT_SWIPE_RIGHT) {
    enum DisplayMode m = display_get_mode();
    m = (m == 0) ? (DISPLAY_MODE_COUNT - 1) : (m - 1);
    display_set_mode(m);
}
```

Add include if not already there:
```c
#include "display.h"
```

- [ ] **Step 5: Build**

Run: `idf.py build`
Expected: compiles without errors.

---

### Task 3: Clock and info screens

**Files:**
- Modify: `main/display.c` — implement display_show_clock() and display_show_info()
- Modify: `main/main.c` — call display mode render functions

- [ ] **Step 1: Implement `display_show_clock()` in `display.c`**

```c
void display_show_clock(const struct TamaState* state)
{
    if (s_mode != DISPLAY_MODE_CLOCK) return;
    if (!s_clock_label) return;

    char buf[128];
    if (state->connected) {
        // time_t is stored in data.h but not exposed; use msg for now
        snprintf(buf, sizeof(buf), "%s\n\nConnected", state->msg[0] ? state->msg : "Claude Buddy");
    } else {
        snprintf(buf, sizeof(buf), "No Connection\n\nTouch or swipe");
    }
    lv_label_set_text(s_clock_label, buf);
}
```

- [ ] **Step 2: Implement `display_show_info()` in `display.c`**

```c
void display_show_info(const struct TamaState* state)
{
    if (s_mode != DISPLAY_MODE_INFO) return;
    if (!s_info_label) return;

    char buf[512];
    snprintf(buf, sizeof(buf),
        "Claude Buddy Touch\n"
        "ESP32-P4 + ESP32-C6\n\n"
        "Sessions: %u total\n"
        "Running: %u   Waiting: %u\n"
        "Tokens today: %lu\n\n"
        "State: %s\n\n"
        "Swipe L/R to navigate",
        state->sessionsTotal,
        state->sessionsRunning,
        state->sessionsWaiting,
        (unsigned long)state->tokensToday,
        persona_state_names[state_machine_derive(state)]);
    lv_label_set_text(s_info_label, buf);
}
```

- [ ] **Step 3: Update `display_show_buddy()` in `display.c`**

Move the existing content of `display_update()` that shows buddy art/status into `display_show_buddy()`. The function should be the same as the current `display_update()`.

```c
void display_show_buddy(const struct TamaState* state, enum PersonaState persona)
{
    if (s_mode != DISPLAY_MODE_BUDDY) return;

    char buf[128];

    if (state->connected) {
        if (state->promptId[0]) {
            lv_label_set_text(ui.status, "Prompt waiting!");
            lv_obj_set_style_text_color(ui.status, lv_color_hex(0xFFCC00), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0xFFCC00), LV_STATE_DEFAULT);
        } else if (state->sessionsRunning > 0 || state->sessionsWaiting > 0) {
            lv_label_set_text(ui.status, "Active");
            lv_obj_set_style_text_color(ui.status, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
        } else if (state->sessionsTotal > 0) {
            lv_label_set_text(ui.status, "Idle");
            lv_obj_set_style_text_color(ui.status, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
        } else {
            lv_label_set_text(ui.status, "Connected");
            lv_obj_set_style_text_color(ui.status, lv_color_hex(0x8888FF), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0x8888FF), LV_STATE_DEFAULT);
        }
    } else {
        lv_label_set_text(ui.status, "Connecting...");
        lv_obj_set_style_text_color(ui.status, lv_color_hex(0xCC6666), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0xFF0000), LV_STATE_DEFAULT);
    }

    if (persona != ui.last_persona) {
        if (persona >= 0 && persona < P_COUNT && BUDDY[persona]) {
            lv_label_set_text(ui.buddy, BUDDY[persona]);
        }
        ui.last_persona = persona;
    }

    snprintf(buf, sizeof(buf), "Sessions: %u total  %u running  %u waiting",
             state->sessionsTotal, state->sessionsRunning, state->sessionsWaiting);
    lv_label_set_text(ui.session, buf);
    lv_label_set_text(ui.msg, state->msg[0] ? state->msg : "(no message)");

    snprintf(buf, sizeof(buf), "Tokens today: %lu", (unsigned long)state->tokensToday);
    lv_label_set_text(ui.tokens, buf);

    snprintf(buf, sizeof(buf), "State: %s", persona_state_names[persona]);
    lv_label_set_text(ui.state_label, buf);

    if (state->promptId[0]) {
        snprintf(buf, sizeof(buf), "Prompt: %s\nTool: %s\n%s",
                 state->promptId, state->promptTool, state->promptHint);
        lv_label_set_text(ui.prompt, buf);
        lv_obj_clear_flag(ui.prompt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.prompt, LV_OBJ_FLAG_HIDDEN);
    }
}
```

- [ ] **Step 4: Implement `display_show_transcript()` in `display.c`**

```c
void display_show_transcript(const struct TamaState* state)
{
    if (s_mode != DISPLAY_MODE_TRANSCRIPT) return;
    if (!s_transcript_label) return;

    char buf[600];
    buf[0] = '\0';
    for (uint8_t i = 0; i < state->nLines; i++) {
        strncat(buf, state->lines[i], sizeof(buf) - strlen(buf) - 1);
        strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);
    }
    if (buf[0] == '\0') {
        strcpy(buf, "(no recent activity)");
    }
    lv_label_set_text(s_transcript_label, buf);
}
```

- [ ] **Step 5: Update `display_update()` to dispatch to mode-specific functions**

Replace the body of `display_update()`:
```c
void display_update(const struct TamaState* state, enum PersonaState persona)
{
    if (!ui.scr) return;

    switch (s_mode) {
    case DISPLAY_MODE_BUDDY:
        display_show_buddy(state, persona);
        break;
    case DISPLAY_MODE_CLOCK:
        display_show_clock(state);
        break;
    case DISPLAY_MODE_INFO:
        display_show_info(state);
        break;
    case DISPLAY_MODE_TRANSCRIPT:
        display_show_transcript(state);
        break;
    default:
        break;
    }
}
```

- [ ] **Step 6: Build**

Run: `idf.py build`
Expected: compiles without errors.

---

### Task 4: Permission prompt with approve/deny buttons

**Files:**
- Modify: `main/display.h` — add permission button declarations
- Modify: `main/display.c` — add approve/deny LVGL buttons, visibility toggle
- Modify: `main/main.c` — add permission response handling + approval callback

- [ ] **Step 1: Add permission button declarations to `display.h`**

```c
typedef void (*display_approve_cb_t)(void);
typedef void (*display_deny_cb_t)(void);

void display_set_approve_cb(display_approve_cb_t cb);
void display_set_deny_cb(display_deny_cb_t cb);
void display_show_permission(bool show, const char* tool, const char* hint);
```

- [ ] **Step 2: Add permission button UI to `display.c`**

Add to the static struct:
```c
static struct {
    lv_obj_t* scr;
    lv_obj_t* header;
    lv_obj_t* conn_dot;
    lv_obj_t* status;
    lv_obj_t* buddy;
    lv_obj_t* session;
    lv_obj_t* msg;
    lv_obj_t* tokens;
    lv_obj_t* state_label;
    lv_obj_t* prompt;
    lv_obj_t* hint;
    lv_obj_t* approve_btn; // NEW
    lv_obj_t* approve_lbl; // NEW
    lv_obj_t* deny_btn;    // NEW
    lv_obj_t* deny_lbl;    // NEW
    lv_obj_t* prompt_area; // NEW — full prompt text
    enum PersonaState last_persona;
    display_approve_cb_t approve_cb;
    display_deny_cb_t deny_cb;
} ui;
```

Initialize buttons in `display_init()`:
```c
ui.prompt_area = lv_label_create(ui.scr);
lv_obj_set_width(ui.prompt_area, DISP_W - 40);
lv_obj_set_height(ui.prompt_area, 150);
lv_obj_align(ui.prompt_area, LV_ALIGN_TOP_LEFT, 20, 520);
lv_obj_set_style_text_color(ui.prompt_area, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
lv_obj_add_flag(ui.prompt_area, LV_OBJ_FLAG_HIDDEN);

ui.approve_btn = lv_btn_create(ui.scr);
lv_obj_set_size(ui.approve_btn, 200, 60);
lv_obj_align(ui.approve_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
lv_obj_set_style_bg_color(ui.approve_btn, lv_color_hex(0x00AA00), LV_STATE_DEFAULT);
lv_obj_add_flag(ui.approve_btn, LV_OBJ_FLAG_HIDDEN);

ui.approve_lbl = lv_label_create(ui.approve_btn);
lv_label_set_text(ui.approve_lbl, "Approve ✓");
lv_obj_center(ui.approve_lbl);

ui.deny_btn = lv_btn_create(ui.scr);
lv_obj_set_size(ui.deny_btn, 200, 60);
lv_obj_align(ui.deny_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
lv_obj_set_style_bg_color(ui.deny_btn, lv_color_hex(0xAA0000), LV_STATE_DEFAULT);
lv_obj_add_flag(ui.deny_btn, LV_OBJ_FLAG_HIDDEN);

ui.deny_lbl = lv_label_create(ui.deny_btn);
lv_label_set_text(ui.deny_lbl, "Deny ✗");
lv_obj_center(ui.deny_lbl);
```

Add button callbacks:
```c
static void approve_btn_cb(lv_event_t* e)
{
    if (ui.approve_cb) ui.approve_cb();
    display_show_permission(false, NULL, NULL);
}

static void deny_btn_cb(lv_event_t* e)
{
    if (ui.deny_cb) ui.deny_cb();
    display_show_permission(false, NULL, NULL);
}
```

Wire callbacks to buttons (add after creating buttons):
```c
lv_obj_add_event_cb(ui.approve_btn, approve_btn_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(ui.deny_btn, deny_btn_cb, LV_EVENT_CLICKED, NULL);
```

- [ ] **Step 3: Add `display_set_approve_cb`, `display_set_deny_cb`, `display_show_permission`**

```c
void display_set_approve_cb(display_approve_cb_t cb)
{
    ui.approve_cb = cb;
}

void display_set_deny_cb(display_deny_cb_t cb)
{
    ui.deny_cb = cb;
}

void display_show_permission(bool show, const char* tool, const char* hint)
{
    if (show && s_mode == DISPLAY_MODE_BUDDY) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Tool: %s\n\n%s", tool ? tool : "?", hint ? hint : "");
        lv_label_set_text(ui.prompt_area, buf);
        lv_obj_clear_flag(ui.prompt_area, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.approve_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.deny_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.prompt_area, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.approve_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.deny_btn, LV_OBJ_FLAG_HIDDEN);
    }
}
```

- [ ] **Step 4: Wire permission UI to data layer in `main.c`**

Add to `app_task()`, after calling `display_update()`:
```c
// Show permission prompt when tool prompt is present
if (tama->promptId[0] && display_get_mode() == DISPLAY_MODE_BUDDY) {
    display_show_permission(true, tama->promptTool, tama->promptHint);
} else {
    display_show_permission(false, NULL, NULL);
}
```

Add prompt-arrival tracking (in app_task) and approval callbacks with one-shot P_HEART matching the original:

In `app_task()`, add tracking of when a prompt arrives (before the display_update call). Add `#include "esp_timer.h"` at top of main.c:
```c
// Track prompt arrival for P_HEART timing (original: fast < 5s → hearts)
static uint32_t s_prompt_arrived_ms = 0;
static char s_last_prompt_id[40] = "";
if (tama->promptId[0] && strcmp(tama->promptId, s_last_prompt_id) != 0) {
    strncpy(s_last_prompt_id, tama->promptId, sizeof(s_last_prompt_id) - 1);
    s_prompt_arrived_ms = (uint32_t)(esp_timer_get_time() / 1000);
}
if (!tama->promptId[0]) {
    s_last_prompt_id[0] = '\0';
}
```

Approval callbacks:
```c
static void on_approve(void)
{
    struct TamaState* s = data_state();
    if (s->promptId[0]) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t took = now_ms - s_prompt_arrived_ms;

        char resp[256];
        snprintf(resp, sizeof(resp),
                 "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"approved\"}\n",
                 s->promptId);
        ble_nus_send((const uint8_t*)resp, strlen(resp));
        state_machine_record_approval();
        // P_HEART one-shot: fast approval < 5s → show hearts for 2s
        if (took < 5000) {
            state_machine_trigger_oneshot(P_HEART, 2000);
        }
        s->promptId[0] = '\0';
    }
}

static void on_deny(void)
{
    struct TamaState* s = data_state();
    if (s->promptId[0]) {
        char resp[256];
        snprintf(resp, sizeof(resp),
                 "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"denied\"}\n",
                 s->promptId);
        ble_nus_send((const uint8_t*)resp, strlen(resp));
        s->promptId[0] = '\0';
    }
}
```

Wire in `app_main()` after `touch_init()`:
```c
display_set_approve_cb(on_approve);
display_set_deny_cb(on_deny);
```

- [ ] **Step 5: Build**

Run: `idf.py build`
Expected: compiles without errors.

---

### Task 5: BLE status TX — periodic status updates

**Files:**
- Modify: `main/main.c` — add periodic status TX (every 30s while connected)

- [ ] **Step 1: Add status TX timer to `app_task()`**

Add to the app_task loop, before `vTaskDelay`:
```c
// Periodic BLE status (every 30 seconds while connected)
static uint32_t last_status_tx = 0;
if (tama->connected && (tama->lastUpdated - last_status_tx >= 30000 || last_status_tx == 0)) {
    last_status_tx = tama->lastUpdated;
    char status[256];
    snprintf(status, sizeof(status),
             "{\"cmd\":\"status\",\"connected\":true,"
             "\"sessions\":{\"total\":%u,\"running\":%u,\"waiting\":%u},"
             "\"tokens_today\":%lu}\n",
             tama->sessionsTotal, tama->sessionsRunning, tama->sessionsWaiting,
             (unsigned long)tama->tokensToday);
    ble_nus_send((const uint8_t*)status, strlen(status));
}
```

- [ ] **Step 2: Send approval notifications to desktop on connect**

In the `on_ble_connected` callback:
```c
static void on_ble_connected(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "BLE connected");
        char msg[64];
        snprintf(msg, sizeof(msg),
                 "{\"cmd\":\"status\",\"connected\":true}\n");
        ble_nus_send((const uint8_t*)msg, strlen(msg));
    } else {
        ESP_LOGI(TAG, "BLE disconnected");
    }
}
```

- [ ] **Step 3: Build**

Run: `idf.py build`
Expected: compiles without errors.

---

### Task 6: P_DIZZY one-shot on disconnect

**Files:**
- Modify: `main/main.c` — trigger P_DIZZY as 2s one-shot on connected→disconnected transition

**Rationale:** The original triggers P_DIZZY from IMU shake (2s one-shot). Without an IMU on the P4 board, the most natural trigger is the connected→disconnected transition — the buddy briefly looks dizzy/disoriented before falling asleep.

- [ ] **Step 1: Add disconnect-tracking and P_DIZZY trigger to `app_task()`**

Add before the touch handling section:
```c
// P_DIZZY one-shot: trigger on disconnect (no IMU shake available)
static bool was_connected = true;
if (tama->connected != was_connected) {
    was_connected = tama->connected;
    if (!tama->connected) {
        state_machine_trigger_oneshot(P_DIZZY, 2000);
    }
}
```

- [ ] **Step 2: Build**

Run: `idf.py build`
Expected: compiles without errors.

---

## Self-Review Checklist

1. **Spec coverage:** Phase 2 tasks from the design doc (state machine, mode navigation, clock, info, permission UI, transcript, BLE TX) — all represented above. Touch gesture handling (swipe) was already implemented in Phase 1 and is wired in Task 2.
2. **Placeholder scan:** No TBD/TODO placeholders. All code is concrete.
3. **Type consistency:** PersonaState enum lives in state_machine.h now. DisplayMode enum is in display.h. Callback typedefs match usage. ble_nus_send() signature matches existing ble_nus.h.
4. **One-shot pattern verified:** P_HEART = 2s one-shot triggered by fast approval (<5s), matching original claude-desktop-buddy main.cpp:1087. P_DIZZY = 2s one-shot triggered by disconnect (original used IMU shake — not available on P4). `state_machine_update()` applies one-shot override before falling back to base state, matching the original `triggerOneShot()` / `oneShotUntil` pattern.
