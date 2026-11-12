#pragma once

#include <stdint.h>
#include "esp_err.h"

/* 初始化背光 PWM，初始占空比为 0，避免上电瞬间点亮。 */
esp_err_t display_backlight_init(void);

/* 设置背光亮度，参数范围为 0~100%，内部会执行安全上限限制。 */
esp_err_t display_backlight_set(uint8_t percent);
