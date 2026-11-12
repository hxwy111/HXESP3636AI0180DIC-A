#pragma once

#include "esp_err.h"

/* Create the LVGL widgets demo once, inside the dedicated LVGL task. */
esp_err_t ui_demo_create(void);
