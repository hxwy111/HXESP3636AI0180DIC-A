#include "display_panel.h"
#include "display_bus.h"
#include "display_config.h"
#include "nv3003a_init.h"

#include <stddef.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static uint8_t s_line_buffer[DISPLAY_WIDTH * 2] __attribute__((aligned(4)));

/* 执行硬件复位，并逐条发送 Arduino 示例中的厂家初始化命令。 */
esp_err_t display_panel_init(void)
{
    esp_err_t err = display_bus_init();
    if (err != ESP_OK) {
        return err;
    }

    /* NV3003A 复位时序：拉低 10 ms，再拉高并等待 120 ms。 */
    gpio_set_level(DISPLAY_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(DISPLAY_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* 初始化表来自 Arduino 最新显示示例，数据内容保持原样。 */
    for (size_t i = 0; i < kNv3003aInitCount; ++i) {
        const Nv3003aInitCmd *item = &kNv3003aInit[i];
        err = display_bus_write_command(item->cmd, item->data, item->data_len);
        if (err != ESP_OK) {
            return err;
        }
        if (item->delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(item->delay_ms));
        }
    }
    return ESP_OK;
}

esp_err_t display_panel_set_window(uint16_t x0, uint16_t y0,
                                   uint16_t x1, uint16_t y1)
{
    /* 窗口坐标均为闭区间，x1/y1 必须落在 360x360 屏幕范围内。 */
    if (x0 > x1 || y0 > y1 || x1 >= DISPLAY_WIDTH || y1 >= DISPLAY_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t columns[] = {
        (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1
    };
    const uint8_t rows[] = {
        (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1
    };

    /* 2Ah 设置列地址，2Bh 设置行地址。 */
    esp_err_t err = display_bus_write_command(0x2a, columns, sizeof(columns));
    if (err != ESP_OK) {
        return err;
    }
    return display_bus_write_command(0x2b, rows, sizeof(rows));
}

esp_err_t display_panel_fill(uint16_t rgb565)
{
    /* 先生成一行相同颜色的 RGB565 数据，后续逐行发送到屏幕。 */
    const uint8_t high = (uint8_t)(rgb565 >> 8);
    const uint8_t low = (uint8_t)rgb565;
    for (int x = 0; x < DISPLAY_WIDTH; ++x) {
        s_line_buffer[x * 2] = high;
        s_line_buffer[x * 2 + 1] = low;
    }

    /* 全屏窗口只设置一次，控制器会随像素流自动递增列、行地址。 */
    esp_err_t err = display_panel_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    if (err != ESP_OK) {
        return err;
    }

    /* 保持 CS 有效，首行发送写显存命令，后续行只发送像素数据。 */
    return display_bus_write_pixels_repeated(s_line_buffer, sizeof(s_line_buffer),
                                             DISPLAY_HEIGHT);
}

esp_err_t display_panel_draw_bitmap(uint16_t x0, uint16_t y0,
                                    uint16_t x1, uint16_t y1,
                                    const void *pixels, size_t length)
{
    if (pixels == NULL || x0 > x1 || y0 > y1 ||
        x1 >= DISPLAY_WIDTH || y1 >= DISPLAY_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t expected = (size_t)(x1 - x0 + 1) * (y1 - y0 + 1) * 2;
    if (length != expected) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 每个矩形只设置一次窗口，分包连续发送由总线层负责。 */
    esp_err_t err = display_panel_set_window(x0, y0, x1, y1);
    if (err != ESP_OK) {
        return err;
    }
    return display_bus_write_pixels(pixels, length);
}
