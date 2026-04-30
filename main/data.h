#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_timer.h"
#include "cJSON.h"

#include "tama_state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Fake {
    const char* name;
    uint8_t total;
    uint8_t running;
    uint8_t waiting;
    bool completed;
    uint32_t tokens;
};

static const struct _Fake s_fakes[] = {
    {"asleep",      0, 0, 0, false, 0},
    {"one idle",    1, 0, 0, false, 12000},
    {"busy",        4, 3, 0, false, 89000},
    {"attention",   2, 1, 1, false, 45000},
    {"completed",   1, 0, 0, true,  142000},
};
#define DATA_N_FAKES (sizeof(s_fakes) / sizeof(s_fakes[0]))

#define DATA_LINE_BUF_SIZE 1024

static char     s_line_buf[DATA_LINE_BUF_SIZE];
static uint16_t s_line_len = 0;
static uint64_t s_last_live_us = 0;
static bool     s_rtc_valid = false;
static time_t   s_current_time = 0;
static bool     s_demo_mode = false;
static uint8_t  s_demo_idx = 0;
static uint64_t s_demo_next = 0;
static struct TamaState s_tama = {0};

static inline uint64_t _now_us(void)
{
    return esp_timer_get_time();
}

static inline uint32_t _now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline void data_set_demo(bool on)
{
    s_demo_mode = on;
    if (on) { s_demo_idx = 0; s_demo_next = _now_us(); }
}

static inline bool data_demo(void) { return s_demo_mode; }

static inline void data_advance_demo(void)
{
    s_demo_idx = (s_demo_idx + 1) % DATA_N_FAKES;
    s_demo_next = _now_us() + 8000000ULL;
}

static inline bool data_connected(void)
{
    return s_last_live_us != 0 && (_now_us() - s_last_live_us) <= 30000000ULL;
}

static inline struct TamaState* data_state(void) { return &s_tama; }

static inline bool data_rtc_valid(void) { return s_rtc_valid; }
static inline time_t data_current_time(void) { return s_current_time; }

static void _apply_json(const char* json_str, struct TamaState* out);

static inline void data_feed_byte(uint8_t byte)
{
    if (byte == '\n' || byte == '\r') {
        if (s_line_len > 0) {
            s_line_buf[s_line_len] = '\0';
            if (s_line_buf[0] == '{') {
                _apply_json(s_line_buf, &s_tama);
            }
            s_line_len = 0;
        }
    } else if (s_line_len < DATA_LINE_BUF_SIZE - 1) {
        s_line_buf[s_line_len++] = (char)byte;
    }
}

static inline void data_feed(const uint8_t* data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        data_feed_byte(data[i]);
    }
}

static void _apply_json(const char* json_str, struct TamaState* out)
{
    cJSON* doc = cJSON_Parse(json_str);
    if (!doc) return;

    cJSON* cmd = cJSON_GetObjectItem(doc, "cmd");
    if (cmd && cJSON_IsString(cmd)) {
        const char* cmd_str = cmd->valuestring;
        if (strcmp(cmd_str, "name") == 0 ||
            strcmp(cmd_str, "species") == 0 ||
            strcmp(cmd_str, "unpair") == 0 ||
            strcmp(cmd_str, "owner") == 0 ||
            strcmp(cmd_str, "char_begin") == 0 ||
            strcmp(cmd_str, "status") == 0) {
            s_last_live_us = _now_us();
            cJSON_Delete(doc);
            return;
        }
        if (strcmp(cmd_str, "permission") != 0) {
            s_last_live_us = _now_us();
            cJSON_Delete(doc);
            return;
        }
    }

    cJSON* time_arr = cJSON_GetObjectItem(doc, "time");
    if (time_arr && cJSON_IsArray(time_arr) && cJSON_GetArraySize(time_arr) == 2) {
        cJSON* t0 = cJSON_GetArrayItem(time_arr, 0);
        cJSON* t1 = cJSON_GetArrayItem(time_arr, 1);
        if (t0 && t1) {
            time_t local = (time_t)t0->valueint + (int32_t)t1->valueint;
            s_current_time = local;
            s_rtc_valid = true;
            s_last_live_us = _now_us();
        }
        cJSON_Delete(doc);
        return;
    }

    cJSON* v;
    v = cJSON_GetObjectItem(doc, "total");
    if (v) out->sessionsTotal = (uint8_t)v->valueint;

    v = cJSON_GetObjectItem(doc, "running");
    if (v) out->sessionsRunning = (uint8_t)v->valueint;

    v = cJSON_GetObjectItem(doc, "waiting");
    if (v) out->sessionsWaiting = (uint8_t)v->valueint;

    v = cJSON_GetObjectItem(doc, "completed");
    if (v) out->recentlyCompleted = (v->type == cJSON_True);

    v = cJSON_GetObjectItem(doc, "tokens_today");
    if (v) out->tokensToday = (uint32_t)v->valueint;

    cJSON* msg = cJSON_GetObjectItem(doc, "msg");
    if (msg && cJSON_IsString(msg)) {
        strncpy(out->msg, msg->valuestring, sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = '\0';
    }

    cJSON* entries = cJSON_GetObjectItem(doc, "entries");
    if (entries && cJSON_IsArray(entries)) {
        uint8_t n = 0;
        int size = cJSON_GetArraySize(entries);
        for (int i = 0; i < size && n < 8; i++) {
            cJSON* entry = cJSON_GetArrayItem(entries, i);
            if (entry && cJSON_IsString(entry)) {
                strncpy(out->lines[n], entry->valuestring, 91);
                out->lines[n][91] = '\0';
                n++;
            }
        }
        if (n != out->nLines || (n > 0 && strcmp(out->lines[n - 1], out->msg) != 0)) {
            out->lineGen++;
        }
        out->nLines = n;
    }

    cJSON* prompt = cJSON_GetObjectItem(doc, "prompt");
    if (prompt && cJSON_IsObject(prompt)) {
        cJSON* pid = cJSON_GetObjectItem(prompt, "id");
        cJSON* pt  = cJSON_GetObjectItem(prompt, "tool");
        cJSON* ph  = cJSON_GetObjectItem(prompt, "hint");
        if (pid && cJSON_IsString(pid)) {
            strncpy(out->promptId, pid->valuestring, sizeof(out->promptId) - 1);
            out->promptId[sizeof(out->promptId) - 1] = '\0';
        }
        if (pt && cJSON_IsString(pt)) {
            strncpy(out->promptTool, pt->valuestring, sizeof(out->promptTool) - 1);
            out->promptTool[sizeof(out->promptTool) - 1] = '\0';
        }
        if (ph && cJSON_IsString(ph)) {
            strncpy(out->promptHint, ph->valuestring, sizeof(out->promptHint) - 1);
            out->promptHint[sizeof(out->promptHint) - 1] = '\0';
        }
    } else {
        out->promptId[0] = '\0';
        out->promptTool[0] = '\0';
        out->promptHint[0] = '\0';
    }

    out->lastUpdated = _now_ms();
    s_last_live_us = _now_us();

    cJSON_Delete(doc);
}

static inline void data_poll(struct TamaState* out)
{
    uint32_t now = _now_ms();

    if (s_demo_mode) {
        uint64_t now_us = _now_us();
        if (now_us >= s_demo_next) {
            s_demo_idx = (s_demo_idx + 1) % DATA_N_FAKES;
            s_demo_next = now_us + 8000000ULL;
        }
        const struct _Fake* s = &s_fakes[s_demo_idx];
        out->sessionsTotal = s->total;
        out->sessionsRunning = s->running;
        out->sessionsWaiting = s->waiting;
        out->recentlyCompleted = s->completed;
        out->tokensToday = s->tokens;
        out->lastUpdated = now;
        out->connected = true;
        snprintf(out->msg, sizeof(out->msg), "demo: %s", s->name);
        return;
    }

    out->connected = data_connected();
    if (!out->connected) {
        out->sessionsTotal = 0;
        out->sessionsRunning = 0;
        out->sessionsWaiting = 0;
        out->recentlyCompleted = false;
        out->lastUpdated = now;
        strncpy(out->msg, "No Claude connected", sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = '\0';
    }
}

#ifdef __cplusplus
}
#endif
