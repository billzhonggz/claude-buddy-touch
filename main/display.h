#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "tama_state.h"

#define DISP_W 480
#define DISP_H 800

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

void display_init(void);
void display_lock(void);
void display_unlock(void);
lv_disp_t* display_get(void);

void display_show_hello(void);
void display_update(const struct TamaState* state, enum PersonaState persona);
