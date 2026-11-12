#pragma once

// 让 LVGL 优先从当前示例目录读取项目专用配置。
#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE
#endif

#include <lvgl.h>

void lvgl_port_init();
void lvgl_port_tick();
