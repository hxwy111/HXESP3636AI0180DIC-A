/* 面板控制接口：复位、初始化、窗口、填色和背光。 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t display_panel_init();
esp_err_t display_set_window(int x0, int y0, int x1, int y1);
esp_err_t display_fill_rect(int x, int y, int width, int height, uint16_t rgb565);
esp_err_t display_fill(uint16_t rgb565);
void display_set_backlight(uint8_t percent);
