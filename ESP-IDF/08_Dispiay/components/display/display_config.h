#pragma once

/* 屏幕分辨率：直径 1.8 英寸圆形屏，NV3003A 控制器。 */
#define DISPLAY_WIDTH             360
#define DISPLAY_HEIGHT            360

/* 单笔 DMA 数据传输最多容纳 20 行，减少 LVGL 局部刷新分包开销。 */
#define DISPLAY_TRANSFER_MAX_BYTES (DISPLAY_WIDTH * 20 * 2)

/* HXESP3636AI0180DIC-A 原理图中的 LCD 网络引脚。 */
#define DISPLAY_SCLK_GPIO         38
#define DISPLAY_QSPI_D0_GPIO      4
#define DISPLAY_QSPI_D1_GPIO      5
#define DISPLAY_QSPI_D2_GPIO      6
#define DISPLAY_QSPI_D3_GPIO      7
#define DISPLAY_CS_GPIO           12
#define DISPLAY_DC_GPIO           9
#define DISPLAY_RESET_GPIO        39
#define DISPLAY_BACKLIGHT_GPIO    10

/* 显示屏使用 ESP32-S3 的 SPI2 外设，以 QSPI 方式发送像素数据。 */
#define DISPLAY_SPI_HOST          SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ      (20 * 1000 * 1000)

/* 背光最大限制为 50%，避免背光电流过大。调用 100% 时实际输出 50%。 */
#define DISPLAY_BACKLIGHT_MAX_PERCENT 50
