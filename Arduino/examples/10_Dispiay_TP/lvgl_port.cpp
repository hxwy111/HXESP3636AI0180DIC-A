#include "lvgl_port.h"

#include "Arduino.h"
#include "display_bus.h"
#include "display_config.h"
#include "display_panel.h"
#include "touch_driver.h"

namespace {
constexpr int kBufferLines = 20;
lv_color_t g_draw_buffer_1[DISPLAY_WIDTH * kBufferLines];
lv_color_t g_draw_buffer_2[DISPLAY_WIDTH * kBufferLines];
uint8_t g_line_bytes[DISPLAY_WIDTH * 2] __attribute__((aligned(4)));
lv_disp_draw_buf_t g_draw_buf;
lv_disp_drv_t g_disp_drv;
lv_indev_drv_t g_touch_drv;
TouchSample g_input_sample = {};
uint32_t g_last_tick_ms = 0;

void touch_read_cb(lv_indev_drv_t * /*drv*/, lv_indev_data_t *data) {
  touch_poll();
  touch_pop_sample(g_input_sample);
  data->point.x = g_input_sample.x;
  data->point.y = g_input_sample.y;
  data->state = g_input_sample.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  data->continue_reading = touch_has_pending_sample();
}

void flush_cb(lv_disp_drv_t * /*drv*/, const lv_area_t *area, lv_color_t *color_p) {
  const int width = area->x2 - area->x1 + 1;
  const int height = area->y2 - area->y1 + 1;
  if (width <= 0 || height <= 0 || width > DISPLAY_WIDTH ||
      area->x1 < 0 || area->y1 < 0 || area->x2 >= DISPLAY_WIDTH ||
      area->y2 >= DISPLAY_HEIGHT) {
    lv_disp_flush_ready(&g_disp_drv);
    return;
  }

  for (int row = 0; row < height; ++row) {
    // Flush is synchronous. Service TP between small row batches so a large
    // redraw cannot starve short taps; touch_poll never calls LVGL APIs.
    if ((row & 7) == 0) touch_poll();
    if (display_set_window(area->x1, area->y1 + row, area->x2,
                           area->y1 + row) != ESP_OK) {
      break;
    }

    // LVGL 的 16 位颜色在 ESP32 内存中按小端排列，面板要求高字节先发，
    // 因此在发送前显式转换为 RGB565 大端字节流。

    for (int col = 0; col < width; ++col) {
      const uint16_t pixel = color_p[row * width + col].full;
      g_line_bytes[col * 2] = static_cast<uint8_t>(pixel >> 8);
      g_line_bytes[col * 2 + 1] = static_cast<uint8_t>(pixel);
    }
    if (display_bus_write_color(g_line_bytes, static_cast<size_t>(width) * 2) != ESP_OK) {
      break;
    }
  }

  lv_disp_flush_ready(&g_disp_drv);
}
}

void lvgl_port_init() {
  lv_init();

  lv_disp_draw_buf_init(&g_draw_buf, g_draw_buffer_1, g_draw_buffer_2,
                        DISPLAY_WIDTH * kBufferLines);
  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = DISPLAY_WIDTH;
  g_disp_drv.ver_res = DISPLAY_HEIGHT;
  g_disp_drv.flush_cb = flush_cb;
  g_disp_drv.draw_buf = &g_draw_buf;
  lv_disp_drv_register(&g_disp_drv);

  lv_indev_drv_init(&g_touch_drv);
  g_touch_drv.type = LV_INDEV_TYPE_POINTER;
  g_touch_drv.read_cb = touch_read_cb;
  lv_indev_t *input = lv_indev_drv_register(&g_touch_drv);
  if (input != nullptr && input->driver->read_timer != nullptr) {
    lv_timer_set_period(input->driver->read_timer, 10);
  }
  g_last_tick_ms = millis();
}

void lvgl_port_tick() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - g_last_tick_ms;
  if (elapsed != 0) {
    lv_tick_inc(elapsed);
    g_last_tick_ms = now;
  }
}
