#pragma once

#include <stdint.h>

struct TouchSample {
  uint16_t raw_x;
  uint16_t raw_y;
  uint16_t x;
  uint16_t y;
  uint32_t time_ms;
  uint8_t contact_id;
  uint8_t gesture;
  bool pressed;
};

struct TouchStatus {
  bool online;
  bool auto_sleep_disabled;
  uint8_t chip_id;
  uint8_t project_id;
  uint8_t firmware_version;
  uint8_t factory_id;
  uint32_t frames;
  uint32_t io_errors;
  uint32_t invalid_frames;
  uint32_t queue_overflows;
  uint16_t observed_x_max;
  uint16_t observed_y_max;
};

// All calls except the internal GPIO ISR belong to the Arduino loop task.
bool touch_init();
void touch_poll();
const TouchStatus &touch_get_status();
const TouchSample &touch_get_latest();
// FIFO preserves press/release order between LVGL input reads.
bool touch_pop_sample(TouchSample &sample);
bool touch_has_pending_sample();
