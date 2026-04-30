#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TamaState {
    uint8_t  sessionsTotal;
    uint8_t  sessionsRunning;
    uint8_t  sessionsWaiting;
    bool     recentlyCompleted;
    uint32_t tokensToday;
    uint32_t lastUpdated;
    char     msg[24];
    bool     connected;
    char     lines[8][92];
    uint8_t  nLines;
    uint16_t lineGen;
    char     promptId[40];
    char     promptTool[20];
    char     promptHint[44];
};

#ifdef __cplusplus
}
#endif
