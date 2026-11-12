#include "esp_err.h"
#include "esp_log.h"

#include "display_backlight.h"
#include "display_panel.h"
#include "lvgl_port.h"
#include "ui_demo.h"
#include "touch_driver.h"

void app_main(void)
{
    /* main 只编排启动流程：硬件初始化 -> LVGL 适配层 -> 圆形仪表盘。 */
    ESP_ERROR_CHECK(display_backlight_init());
    ESP_ERROR_CHECK(display_backlight_set(0));
    ESP_ERROR_CHECK(display_panel_init());

    /* Missing TP keeps the display usable; the driver retries in the LVGL task. */
    esp_err_t touch_err = touch_init();
    if (touch_err != ESP_OK) {
        ESP_LOGE("DISPLAY_APP", "TP resource initialization failed: %s",
                 esp_err_to_name(touch_err));
    }

    /* LVGL 任务创建界面并刷新首帧，然后打开背光。 */
    ESP_ERROR_CHECK(lvgl_port_start(ui_demo_create));
    ESP_LOGI("DISPLAY_APP", "LVGL task started, waiting for first frame");
}
