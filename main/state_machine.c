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

static enum PersonaState s_oneshot_state = P_COUNT;
static uint32_t s_oneshot_until_ms = 0;
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
