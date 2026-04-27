#include "display.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char* TAG = "display";

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing display with LVGL...");

    bsp_display_start();
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Display initialized: %dx%d", DISP_W, DISP_H);
}

void display_lock(void)
{
    bsp_display_lock(-1);
}

void display_unlock(void)
{
    bsp_display_unlock();
}

lv_disp_t* display_get(void)
{
    return lv_disp_get_default();
}

void display_show_hello(void)
{
    display_lock();

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_STATE_DEFAULT);

    lv_obj_t* label = lv_label_create(scr);
    lv_label_set_text(label, "Claude Buddy Touch\n\nConnecting...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_center(label);

    lv_scr_load(scr);

    display_unlock();
}
