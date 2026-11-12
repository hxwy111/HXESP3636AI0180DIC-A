#include "lvgl_port.h"

#include <stdbool.h>
#include "lvgl.h"
#include "display_config.h"
#include "display_panel.h"
#include "display_backlight.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "touch_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LVGL_BUFFER_LINES DISPLAY_BUFFER_LINES
#define LVGL_REFRESH_PERIOD_MS 16
#define LVGL_BUFFER_PIXELS (DISPLAY_WIDTH * LVGL_BUFFER_LINES)
#define LVGL_TASK_STACK_BYTES 8192

_Static_assert(sizeof(lv_color_t) == 2, "The panel requires RGB565");
_Static_assert(LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 1,
               "The panel requires RGB565 in big-endian byte order");

static const char *TAG = "LVGL_PORT";
static lv_color_t *s_buffers[2];
static lv_disp_draw_buf_t s_draw_buffer;
static lv_disp_drv_t s_display_driver;
static lv_disp_t *s_display;
static TaskHandle_t s_task;
static lvgl_port_ui_create_cb_t s_create_ui;
static esp_err_t s_flush_error;
static lv_disp_drv_t *s_pending_flush;
static lv_indev_drv_t s_touch_driver;
static lv_indev_t *s_touch_input;
static touch_sample_t s_input_sample;
#if !LV_TICK_CUSTOM
static int64_t s_last_tick_us;
#endif

static void update_tick(void)
{
#if !LV_TICK_CUSTOM
    int64_t now = esp_timer_get_time();
    uint32_t elapsed_ms = (uint32_t)((now - s_last_tick_us) / 1000);
    if (elapsed_ms != 0) {
        lv_tick_inc(elapsed_ms);
        s_last_tick_us += (int64_t)elapsed_ms * 1000;
    }
#endif
}

static void touch_read_cb(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    touch_poll();
    touch_pop_sample(&s_input_sample);
    data->point.x = s_input_sample.x;
    data->point.y = s_input_sample.y;
    data->state = s_input_sample.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->continue_reading = touch_has_pending_sample();
}

/* Complete DMA on the LVGL task; no LVGL or I2C work runs in the SPI ISR. */
static void finish_flush(bool wait)
{
    if (s_pending_flush == NULL) {
        return;
    }
    esp_err_t err = display_panel_wait_flush(wait);
    if (err == ESP_ERR_TIMEOUT) {
        return;
    }
    /* A failed join cannot safely release or reuse a DMA-owned draw buffer. */
    ESP_ERROR_CHECK(err);
    lv_disp_drv_t *driver = s_pending_flush;
    s_pending_flush = NULL;
    lv_disp_flush_ready(driver);
}

static void wait_cb(lv_disp_drv_t *driver)
{
    (void)driver;
    touch_poll();
    finish_flush(true);
}

static void flush_cb(lv_disp_drv_t *driver, const lv_area_t *area,
                     lv_color_t *colors)
{
    /* LVGL calls wait_cb before reusing either of the two partial buffers. */
    touch_poll();
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (area->x1 >= 0 && area->y1 >= 0 && area->x2 >= area->x1 &&
        area->y2 >= area->y1 && area->x2 < DISPLAY_WIDTH &&
        area->y2 < DISPLAY_HEIGHT) {
        const size_t bytes = (size_t)(area->x2 - area->x1 + 1) *
                             (area->y2 - area->y1 + 1) * sizeof(lv_color_t);
        err = display_panel_draw_bitmap_async(area->x1, area->y1,
                                               area->x2, area->y2, colors, bytes);
    }
    if (err == ESP_OK) {
        s_pending_flush = driver;
        return;  /* Render the next strip while SPI DMA sends this one. */
    }
    if (s_flush_error == ESP_OK) {
        ESP_LOGE(TAG, "Panel flush failed: %s", esp_err_to_name(err));
    }
    s_flush_error = err;
    lv_disp_flush_ready(driver);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    esp_err_t err = s_create_ui();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UI creation failed: %s", esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* 首帧绘制完成后再点亮背光，避免启动时显示显存残留。 */
    lv_refr_now(s_display);
    finish_flush(true);
    ESP_ERROR_CHECK(s_flush_error);
    ESP_ERROR_CHECK(display_backlight_set(100));
    ESP_LOGI(TAG, "LVGL %d.%d.%d first frame ready: 360x360 round panel",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    for (;;) {
        touch_poll();
        update_tick();
        s_flush_error = ESP_OK;
        uint32_t delay_ms = lv_timer_handler();
        const uint32_t handler_end_tick = lv_tick_get();
        finish_flush(true);
        update_tick();
        /* Account only for elapsed time not reflected in the handler's result.
         * With a task-driven tick this also includes the rendering time. */
        const uint32_t elapsed_ms = lv_tick_elaps(handler_end_tick);
        delay_ms = delay_ms > elapsed_ms ? delay_ms - elapsed_ms : 1;
        if (s_flush_error != ESP_OK) {
            /* 传输失败后，下轮重新绘制整个界面，恢复丢失的刷新区域。 */
            lv_obj_invalidate(lv_disp_get_scr_act(s_display));
        }
        /* 有任务时及时处理，空闲时也至少每 10 ms 检查一次。 */
        if (delay_ms < 1) {
            delay_ms = 1;
        } else if (delay_ms > 10) {
            delay_ms = 10;
        }
        TickType_t ticks = pdMS_TO_TICKS(delay_ms);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}

esp_err_t lvgl_port_start(lvgl_port_ui_create_cb_t create_ui)
{
    if (create_ui == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 显式放入内部 DMA RAM，不让 malloc 将像素缓冲区分配到 PSRAM。 */
    const size_t bytes = LVGL_BUFFER_PIXELS * sizeof(lv_color_t);
    for (int i = 0; i < 2; ++i) {
        s_buffers[i] = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_buffers[i] == NULL) {
            for (int j = 0; j < 2; ++j) {
                heap_caps_free(s_buffers[j]);
                s_buffers[j] = NULL;
            }
            return ESP_ERR_NO_MEM;
        }
    }

    lv_init();
    /* Logging callback is optional in Component Manager LVGL builds. */
    lv_disp_draw_buf_init(&s_draw_buffer, s_buffers[0], s_buffers[1], LVGL_BUFFER_PIXELS);
    lv_disp_drv_init(&s_display_driver);
    /* 圆形可视区对应的显存、LVGL 逻辑坐标仍是 360x360 矩形。 */
    s_display_driver.hor_res = DISPLAY_WIDTH;
    s_display_driver.ver_res = DISPLAY_HEIGHT;
    s_display_driver.flush_cb = flush_cb;
    s_display_driver.wait_cb = wait_cb;
    s_display_driver.draw_buf = &s_draw_buffer;
    s_display = lv_disp_drv_register(&s_display_driver);
    if (s_display == NULL) {
        lv_deinit();
        for (int i = 0; i < 2; ++i) {
            heap_caps_free(s_buffers[i]);
            s_buffers[i] = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    lv_timer_set_period(_lv_disp_get_refr_timer(s_display), LVGL_REFRESH_PERIOD_MS);

    lv_indev_drv_init(&s_touch_driver);
    s_touch_driver.type = LV_INDEV_TYPE_POINTER;
    s_touch_driver.disp = s_display;
    s_touch_driver.read_cb = touch_read_cb;
    s_touch_input = lv_indev_drv_register(&s_touch_driver);
    if (s_touch_input == NULL) {
        lv_disp_remove(s_display);
        s_display = NULL;
        lv_deinit();
        for (int i = 0; i < 2; ++i) {
            heap_caps_free(s_buffers[i]);
            s_buffers[i] = NULL;
        }
        return ESP_ERR_NO_MEM;
    }
    if (s_touch_input->driver->read_timer != NULL) {
        lv_timer_set_period(s_touch_input->driver->read_timer, 10);
    }
#if !LV_TICK_CUSTOM
    s_last_tick_us = esp_timer_get_time();
#endif
    s_create_ui = create_ui;
    s_flush_error = ESP_OK;
    if (xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_BYTES, NULL, 5, &s_task) != pdPASS) {
        lv_disp_remove(s_display);
        s_display = NULL;
        lv_deinit();
        for (int i = 0; i < 2; ++i) {
            heap_caps_free(s_buffers[i]);
            s_buffers[i] = NULL;
        }
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Started: two %u-byte internal DMA draw buffers, asynchronous QSPI, refresh=%ums",
             (unsigned)bytes, (unsigned)LVGL_REFRESH_PERIOD_MS);
    return ESP_OK;
}
