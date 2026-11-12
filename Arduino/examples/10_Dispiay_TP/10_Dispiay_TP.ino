/* 360 x 360 round NV3003A display + CST816D touch, LVGL 8.x. */
#include "Arduino.h"
#include <stdio.h>
#include <string.h>
#include "display_config.h"
#include "display_panel.h"
#include "lvgl_port.h"
#include "touch_driver.h"

namespace {
constexpr int kTargetCount = 5;
const int kTargetX[kTargetCount] = {180, 180, 290, 180, 70};
const int kTargetY[kTargetCount] = {180, 70, 180, 290, 180};
const char *const kTargetText[kTargetCount] = {"C", "U", "R", "D", "L"};

bool g_display_ready = false;
bool g_test_page = false;
uint8_t g_brightness = 60;
uint8_t g_test_mask = 0;
uint32_t g_taps = 0;
uint32_t g_last_ui_ms = 0;
int g_last_online = -1;
lv_obj_t *g_home = nullptr;
lv_obj_t *g_test = nullptr;
lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_slider = nullptr;
lv_obj_t *g_value_label = nullptr;
lv_obj_t *g_tap_label = nullptr;
lv_obj_t *g_raw_label = nullptr;
lv_obj_t *g_xy_label = nullptr;
lv_obj_t *g_progress_label = nullptr;
lv_obj_t *g_error_label = nullptr;
lv_obj_t *g_marker = nullptr;
lv_obj_t *g_targets[kTargetCount] = {};

lv_color_t background() { return lv_color_hex(0x0D1420); }
lv_color_t surface() { return lv_color_hex(0x1B293C); }
lv_color_t accent() { return lv_color_hex(0x43CFBD); }
lv_color_t text_main() { return lv_color_hex(0xEAF1FA); }
lv_color_t text_dim() { return lv_color_hex(0x97ABC3); }

void set_label(lv_obj_t *label, const char *text) {
  // Repaint only when content actually changes.
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int width,
                     const char *text, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, width);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
  lv_label_set_text(label, text);
  return label;
}

lv_obj_t *make_container(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int width, int height,
                      const char *text, lv_event_cb_t callback) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_radius(button, 14, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(button, surface(), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x285C65), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x355069), 0);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_t *label = make_label(button, 0, 0, width, text, text_main());
  lv_obj_center(label);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  return button;
}

void update_brightness() {
  char text[40];
  snprintf(text, sizeof(text), "%u %%", static_cast<unsigned>(g_brightness));
  set_label(g_value_label, text);
  display_set_backlight(g_brightness);
}

void slider_event(lv_event_t *event) {
  g_brightness = static_cast<uint8_t>(lv_slider_get_value(lv_event_get_target(event)));
  update_brightness();
}

void update_test_progress() {
  uint8_t count = 0;
  for (int i = 0; i < kTargetCount; ++i) {
    if (g_test_mask & (1U << i)) ++count;
    lv_obj_set_style_bg_color(g_targets[i],
        (g_test_mask & (1U << i)) ? lv_color_hex(0x267C70) : surface(), 0);
  }
  char text[32];
  snprintf(text, sizeof(text), "5-POINT TEST  %u / 5", static_cast<unsigned>(count));
  set_label(g_progress_label, text);
}

void target_event(lv_event_t *event) {
  for (int i = 0; i < kTargetCount; ++i) {
    if (lv_event_get_target(event) == g_targets[i]) g_test_mask |= 1U << i;
  }
  update_test_progress();
}

void reset_test_event(lv_event_t * /*event*/) {
  g_test_mask = 0;
  update_test_progress();
}

void show_page(bool test_page) {
  g_test_page = test_page;
  if (test_page) {
    g_test_mask = 0;
    update_test_progress();
    lv_obj_add_flag(g_home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_test, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_test, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_home, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_add_flag(g_marker, LV_OBJ_FLAG_HIDDEN);
  g_last_ui_ms = millis() - 80;
}

void open_test_event(lv_event_t * /*event*/) { show_page(true); }
void back_event(lv_event_t * /*event*/) { show_page(false); }
void tap_event(lv_event_t * /*event*/) {
  char text[32];
  snprintf(text, sizeof(text), "TAP  %lu", static_cast<unsigned long>(++g_taps));
  set_label(g_tap_label, text);
}

void create_home(lv_obj_t *screen) {
  g_home = make_container(screen, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  make_label(g_home, 85, 40, 190, "ROUND TOUCH", text_main());
  g_status_label = make_label(g_home, 85, 69, 190, "TP STARTING", text_dim());

  lv_obj_t *card = make_container(g_home, 66, 103, 228, 100);
  lv_obj_set_style_bg_color(card, surface(), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 20, 0);
  make_label(card, 10, 12, 208, "BACKLIGHT", text_dim());
  g_value_label = make_label(card, 30, 38, 168, "60 %", accent());
#if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(g_value_label, &lv_font_montserrat_28, 0);
#endif
  char limit[40];
  snprintf(limit, sizeof(limit), "PWM limit: %u%%",
           static_cast<unsigned>(DISPLAY_BACKLIGHT_LIMIT_PERCENT));
  make_label(card, 10, 77, 208, limit, text_dim());

  g_slider = lv_slider_create(g_home);
  lv_obj_set_pos(g_slider, 78, 221);
  lv_obj_set_size(g_slider, 204, 14);
  lv_slider_set_range(g_slider, 10, 100);  // Keep the page visible during testing.
  lv_slider_set_value(g_slider, g_brightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_slider, surface(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_slider, accent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_slider, text_main(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(g_slider, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(g_slider, 15);
  lv_obj_add_event_cb(g_slider, slider_event, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *tap = make_button(g_home, 82, 263, 92, 44, "TAP  0", tap_event);
  g_tap_label = lv_obj_get_child(tap, 0);
  make_button(g_home, 186, 263, 92, 44, "TP TEST", open_test_event);
  make_label(g_home, 100, 316, 160, "360 x 360 / CST816D", text_dim());
}

void create_test(lv_obj_t *screen) {
  g_test = make_container(screen, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  g_progress_label = make_label(g_test, 82, 26, 196, "5-POINT TEST  0 / 5", text_main());
  g_raw_label = make_label(g_test, 80, 106, 200, "RAW: --- , ---", text_dim());
  g_xy_label = make_label(g_test, 80, 129, 200, "XY: --- , ---  UP", text_dim());
  g_error_label = make_label(g_test, 58, 226, 244, "I2C:0 BAD:0 Q:0", text_dim());
  make_button(g_test, 78, 258, 72, 44, "BACK", back_event);
  make_button(g_test, 210, 258, 72, 44, "RESET", reset_test_event);
  for (int i = 0; i < kTargetCount; ++i) {
    g_targets[i] = make_button(g_test, kTargetX[i] - 22, kTargetY[i] - 22,
                               44, 44, kTargetText[i], target_event);
    lv_obj_set_style_radius(g_targets[i], LV_RADIUS_CIRCLE, 0);
  }
  lv_obj_add_flag(g_test, LV_OBJ_FLAG_HIDDEN);
}

void create_screen() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, background(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  create_home(screen);
  create_test(screen);
  g_marker = make_container(screen, 0, 0, 12, 12);
  lv_obj_set_style_radius(g_marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(g_marker, 2, 0);
  lv_obj_set_style_border_color(g_marker, lv_color_hex(0xFFD166), 0);
  lv_obj_add_flag(g_marker, LV_OBJ_FLAG_HIDDEN);
}

void refresh_ui() {
  const uint32_t now = millis();
  if (now - g_last_ui_ms < 80) return;
  g_last_ui_ms = now;
  const TouchStatus &status = touch_get_status();
  const TouchSample &sample = touch_get_latest();
  char text[64];
  if (status.online) {
    snprintf(text, sizeof(text), "TP OK   ID:%02X  FW:%02X",
             status.chip_id, status.firmware_version);
  } else {
    snprintf(text, sizeof(text), "TP OFFLINE - RETRYING");
  }
  set_label(g_status_label, text);
  if (g_last_online != status.online) {
    lv_obj_set_style_text_color(g_status_label,
        status.online ? accent() : lv_color_hex(0xFF907A), 0);
    g_last_online = status.online;
  }
  if (!g_test_page) return;
  snprintf(text, sizeof(text), "RAW: %u , %u", sample.raw_x, sample.raw_y);
  set_label(g_raw_label, text);
  snprintf(text, sizeof(text), "XY: %u , %u  %s", sample.x, sample.y,
           sample.pressed ? "DOWN" : "UP");
  set_label(g_xy_label, text);
  if (status.online) {
    snprintf(text, sizeof(text), "I2C:%lu BAD:%lu Q:%lu",
             static_cast<unsigned long>(status.io_errors),
             static_cast<unsigned long>(status.invalid_frames),
             static_cast<unsigned long>(status.queue_overflows));
  } else {
    snprintf(text, sizeof(text), "TP OFFLINE - RETRYING");
  }
  set_label(g_error_label, text);
  if (sample.pressed) {
    lv_obj_set_pos(g_marker, static_cast<int>(sample.x) - 6,
                             static_cast<int>(sample.y) - 6);
    lv_obj_clear_flag(g_marker, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_marker, LV_OBJ_FLAG_HIDDEN);
  }
}

void handle_serial() {
  // Diagnostics stay reachable even if TP orientation/range is incorrect.
  for (int count = 0; count < 8 && Serial.available(); ++count) {
    const int command = Serial.read();
    if (command == 't' || command == 'T') show_page(true);
    else if (command == 'h' || command == 'H') show_page(false);
    else if (command == 'r' || command == 'R') reset_test_event(nullptr);
    else if (command == 's' || command == 'S') {
      const TouchStatus &status = touch_get_status();
      const TouchSample &sample = touch_get_latest();
      Serial.printf("[TP] online=%u raw=(%u,%u) xy=(%u,%u) down=%u max=(%u,%u)\n",
                    status.online ? 1U : 0U, sample.raw_x, sample.raw_y,
                    sample.x, sample.y, sample.pressed ? 1U : 0U,
                    status.observed_x_max, status.observed_y_max);
      Serial.printf("[TP] frames=%lu io=%lu invalid=%lu overflow=%lu\n",
                    static_cast<unsigned long>(status.frames),
                    static_cast<unsigned long>(status.io_errors),
                    static_cast<unsigned long>(status.invalid_frames),
                    static_cast<unsigned long>(status.queue_overflows));
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("[APP] NV3003A 360x360 + CST816D / LVGL 8");
  Serial.println("[APP] Serial commands: T=test H=home R=reset test S=TP status");
  if (display_panel_init() != ESP_OK) {
    display_set_backlight(0);
    Serial.println("[LCD] Initialization failed");
    return;
  }
  touch_init();  // TP failure keeps display and automatic retry available.
  lvgl_port_init();
  create_screen();
  update_brightness();
  g_display_ready = true;
  g_last_ui_ms = millis() - 80;
  refresh_ui();
}

void loop() {
  if (!g_display_ready) { delay(1000); return; }
  touch_poll();
  handle_serial();
  lvgl_port_tick();
  lv_timer_handler();
  refresh_ui();
  delay(2);
}
