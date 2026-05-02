#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "tama_state.h"

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

enum PersonaState state_machine_derive(const struct TamaState* state);

enum PersonaState state_machine_update(enum PersonaState base_state);

void state_machine_trigger_oneshot(enum PersonaState state, uint32_t dur_ms);

bool state_machine_in_oneshot(void);

void state_machine_record_approval(void);

uint32_t state_machine_ms_since_approval(void);
