#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* 初始化 QSPI 总线和屏幕 SPI 设备。 */
esp_err_t display_bus_init(void);

/* 发送 NV3003A 寄存器命令及参数。 */
esp_err_t display_bus_write_command(uint8_t command, const void *data, size_t length);

/* 同步发送 RGB565 像素流，分包期间保持 CS 有效，长度必须为偶数。 */
esp_err_t display_bus_write_pixels(const void *data, size_t length);

/* 在同一次显存写入中重复发送缓冲区，用于复用一行数据填充整屏。 */
esp_err_t display_bus_write_pixels_repeated(const void *data, size_t length,
                                           size_t repeat_count);

/* 释放 SPI 设备和总线资源。 */
void display_bus_deinit(void);
