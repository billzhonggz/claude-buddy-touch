#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#define DISP_W 480
#define DISP_H 800

void display_init(void);
void display_lock(void);
void display_unlock(void);
lv_disp_t* display_get(void);

void display_show_hello(void);
