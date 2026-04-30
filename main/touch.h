#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TOUCH_EVENT_NONE,
    TOUCH_EVENT_TAP,
    TOUCH_EVENT_LONG_PRESS,
    TOUCH_EVENT_SWIPE_LEFT,
    TOUCH_EVENT_SWIPE_RIGHT,
} touch_event_t;

typedef struct {
    touch_event_t event;
    uint16_t x;
    uint16_t y;
} touch_event_data_t;

void touch_init(void);
touch_event_data_t touch_process(void);
