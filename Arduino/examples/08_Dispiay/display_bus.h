/* 显示总线接口：上层通过这些函数访问 SPI/QSPI。 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t display_bus_init();
esp_err_t display_bus_write_command(uint8_t command, const void *data, size_t length);
esp_err_t display_bus_write_color(const void *data, size_t length);
void display_bus_deinit();
