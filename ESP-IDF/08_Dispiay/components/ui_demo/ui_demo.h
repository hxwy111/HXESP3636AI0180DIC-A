#pragma once

#include "esp_err.h"

/* 创建适用于直径 1.8 英寸圆形屏的仪表盘，必须在 LVGL 任务中调用一次。 */
esp_err_t ui_demo_create(void);
