#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>
#include <stdlib.h>

/* 统一在 LVGL 内生成高字节在前的 RGB565，flush 中不再交换字节。 */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1
#define LV_COLOR_SCREEN_TRANSP 0

/* LVGL 对象/绘制临时数据使用独立内存池，像素 DMA 缓冲区由 port 分配。 */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)
#define LV_MEMCPY_MEMSET_STD 1

/* 从 ESP-IDF 单调时钟读取毫秒时基，无需额外的 1 ms 定时器任务。 */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)(esp_timer_get_time() / 1000LL))
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_DPI_DEF 200
#define LV_DRAW_COMPLEX 1

/* 首版使用内置英文字体；中文界面需要后续添加中文字库。 */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_ASSERT_HANDLER abort();

/* 按需保留圆弧、标签和基础对象，不编译官方大型 demo。 */
#define LV_USE_ARC 1
#define LV_USE_LABEL 1
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif
