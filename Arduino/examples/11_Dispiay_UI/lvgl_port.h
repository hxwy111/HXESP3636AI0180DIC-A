#pragma once

#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE
#endif

#include <lvgl.h>

#if LVGL_VERSION_MAJOR != 8
#error "This display/TP port requires LVGL 8.x."
#endif

bool lvgl_port_init();
void lvgl_port_tick();
void lvgl_port_wait_touch_release();
