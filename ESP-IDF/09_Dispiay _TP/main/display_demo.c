#include "display_demo.h"

#include "display_panel.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 屏幕硬件验证任务：依次显示红、绿、蓝、白、黑五种纯色。 */
static void display_demo_task(void *arg)
{
    (void)arg;
    /* RGB565 格式：红色、绿色、蓝色、白色、黑色。 */
    static const uint16_t colors[] = { 0xf800, 0x07e0, 0x001f, 0xffff, 0x0000 };
    size_t color_index = 0;

    for (;;) {
        /* 填充失败时保留任务，稍后重试，避免直接退出任务。 */
        if (display_panel_fill(colors[color_index]) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        /* 每种颜色保持 1 秒，然后切换到下一种颜色。 */
        color_index = (color_index + 1) % (sizeof(colors) / sizeof(colors[0]));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t display_demo_start(void)
{
    /* 使用独立 FreeRTOS 任务执行显示操作，避免阻塞 app_main。 */
    return xTaskCreate(display_demo_task, "display_demo", 4096, NULL, 5, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
