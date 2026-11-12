#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t raw_x, raw_y;
    uint16_t x, y;
    uint32_t time_ms;
    uint8_t contact_id, gesture;
    bool pressed;
} touch_sample_t;

typedef struct {
    bool online;
    bool auto_sleep_disabled;
    uint8_t chip_id, project_id, firmware_version, factory_id;
    uint32_t frames, io_errors, invalid_frames, queue_overflows;
    uint16_t observed_x_max, observed_y_max;
} touch_status_t;

/* Init before starting the LVGL task. Missing TP is retried automatically.
 * Resource failures return an error; missing chip leaves status offline.
 * All subsequent calls belong to the same LVGL task, including flush_cb.
 * The GPIO ISR only sets a flag; no I2C or LVGL is called from the ISR. */
esp_err_t touch_init(void);
void touch_poll(void);
const touch_status_t *touch_get_status(void);
const touch_sample_t *touch_get_latest(void);
bool touch_pop_sample(touch_sample_t *sample);
bool touch_has_pending_sample(void);

#ifdef __cplusplus
}
#endif
