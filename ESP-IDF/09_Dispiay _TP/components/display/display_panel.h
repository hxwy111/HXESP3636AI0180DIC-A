#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* 复位屏幕并执行厂家提供的 NV3003A 初始化序列。 */
esp_err_t display_panel_init(void);

/* 设置后续显存写入的列地址和行地址窗口。 */
esp_err_t display_panel_set_window(uint16_t x0, uint16_t y0,
                                   uint16_t x1, uint16_t y1);

/* 用指定的 RGB565 颜色填充整块屏幕。 */
esp_err_t display_panel_fill(uint16_t rgb565);

/*
 * 同步刷新闭区间矩形。数据为连续的 RGB565 高字节在前，无额外行跨度。
 * length 必须等于窗口像素数乘以 2，调用期间缓冲区不可修改。
 * 窗口设置和像素发送应由同一显示任务执行，避免多个任务交叉操作显存。
 */
esp_err_t display_panel_draw_bitmap(uint16_t x0, uint16_t y0,
                                    uint16_t x1, uint16_t y1,
                                    const void *pixels, size_t length);

/* Same rectangle convention as draw_bitmap; buffer owned until wait_flush.
 * Maximum length is DISPLAY_TRANSFER_MAX_BYTES. All calls use one task. */
esp_err_t display_panel_draw_bitmap_async(uint16_t x0, uint16_t y0,
                                          uint16_t x1, uint16_t y1,
                                          const void *pixels, size_t length);
esp_err_t display_panel_wait_flush(bool wait);
