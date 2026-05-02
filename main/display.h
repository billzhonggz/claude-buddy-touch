#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "tama_state.h"
#include "state_machine.h"

#define DISP_W 480
#define DISP_H 800

void display_init(void);
void display_lock(void);
void display_unlock(void);
lv_disp_t* display_get(void);

void display_show_hello(void);
void display_update(const struct TamaState* state, enum PersonaState persona);

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

typedef void (*display_approve_cb_t)(void);
typedef void (*display_deny_cb_t)(void);

void display_set_approve_cb(display_approve_cb_t cb);
void display_set_deny_cb(display_deny_cb_t cb);
void display_show_permission(bool show, const char* tool, const char* hint);
