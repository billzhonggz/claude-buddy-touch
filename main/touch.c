#include "touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char* TAG = "touch";

static lv_indev_t* s_indev = NULL;

// Touch tracking state
static bool     s_touching = false;
static uint32_t s_touch_start_ms = 0;
static uint16_t s_touch_x = 0;
static uint16_t s_touch_y = 0;
static bool     s_long_fired = false;

// Swipe detection
static bool     s_swipe_armed = false;
static uint16_t s_swipe_start_x = 0;
static uint16_t s_swipe_start_y = 0;
static uint32_t s_swipe_start_ms = 0;

extern lv_indev_t* bsp_display_get_input_dev(void);

static uint32_t _now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void touch_init(void)
{
    s_indev = bsp_display_get_input_dev();
    if (s_indev) {
        ESP_LOGI(TAG, "touch indev initialized");
    } else {
        ESP_LOGW(TAG, "touch indev not available");
    }
}

touch_event_data_t touch_process(void)
{
    touch_event_data_t result = { TOUCH_EVENT_NONE, 0, 0 };

    if (!s_indev) return result;

    lv_point_t point;
    lv_indev_get_point(s_indev, &point);
    bool pressed = (lv_indev_get_state(s_indev) == LV_INDEV_STATE_PRESSED);

    uint32_t now = _now_ms();

    if (pressed && !s_touching) {
        s_touching = true;
        s_touch_start_ms = now;
        s_touch_x = point.x;
        s_touch_y = point.y;
        s_long_fired = false;
        s_swipe_armed = true;
        s_swipe_start_x = point.x;
        s_swipe_start_y = point.y;
        s_swipe_start_ms = now;

    } else if (pressed && s_touching) {
        uint32_t dur = now - s_touch_start_ms;
        if (dur > 500 && !s_long_fired) {
            s_long_fired = true;
            result.event = TOUCH_EVENT_LONG_PRESS;
            result.x = point.x;
            result.y = point.y;
        }

        if (s_swipe_armed) {
            int dx = (int)point.x - (int)s_swipe_start_x;
            int dy = (int)point.y - (int)s_swipe_start_y;
            uint32_t elapsed = now - s_swipe_start_ms;
            if (elapsed < 500 && (dx * dx + dy * dy) > 1600) {
                if (abs(dx) > abs(dy)) {
                    if (dx > 0) result.event = TOUCH_EVENT_SWIPE_RIGHT;
                    else result.event = TOUCH_EVENT_SWIPE_LEFT;
                    s_swipe_armed = false;
                }
            }
        }

    } else if (!pressed && s_touching) {
        uint32_t dur = now - s_touch_start_ms;
        s_touching = false;
        s_swipe_armed = false;

        if (dur < 300 && !s_long_fired) {
            result.event = TOUCH_EVENT_TAP;
            result.x = s_touch_x;
            result.y = s_touch_y;
        }
    }

    return result;
}
