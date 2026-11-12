/* HXESP3636AI0180DIC-A LVGL UI demo: boot, display, widgets and device pages. */
#include "Arduino.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display_panel.h"
#include "lvgl_port.h"
#include "display_config.h"
#include "touch_driver.h"

namespace {
constexpr uint32_t kBootDurationMs = 2600;
constexpr uint32_t kHomeTickMs = 40;
constexpr uint32_t kColorPeriodMs = 1000;
constexpr uint32_t kPageChangeGuardMs = 400;
constexpr uint32_t kTouchLabelPeriodMs = 40;

// Visible dots have separate 36px touch targets inside the circular screen.
constexpr int kNavItemCount = 3;
constexpr int kNavDotX[kNavItemCount] = {136, 176, 216};
constexpr int kNavDotY = 328;
constexpr int kNavDotSize = 8;
constexpr int kNavTouchSize = 36;
constexpr int kWidgetSliderX = 48;
constexpr int kWidgetSliderY = 232;
constexpr int kWidgetSliderWidth = 264;
constexpr int kWidgetSliderHeight = 18;

const lv_color_t kBgColor = LV_COLOR_MAKE(0xF5, 0xF7, 0xFA);
const lv_color_t kCardColor = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);
const lv_color_t kTextColor = LV_COLOR_MAKE(0x26, 0x32, 0x38);
const lv_color_t kMutedColor = LV_COLOR_MAKE(0x60, 0x7D, 0x8B);
const lv_color_t kAccentColor = LV_COLOR_MAKE(0x5B, 0x8D, 0xEF);

const lv_color_t kHomeColors[] = {
    LV_COLOR_MAKE(0xF0, 0x50, 0x55), LV_COLOR_MAKE(0x55, 0xC9, 0x8A),
    LV_COLOR_MAKE(0x55, 0x8D, 0xE8), LV_COLOR_MAKE(0xF0, 0xC8, 0x65),
    LV_COLOR_MAKE(0x20, 0x20, 0x24), LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)};
const char *const kHomeColorNames[] = {
    "RED", "GREEN", "BLUE", "YELLOW", "BLACK", "WHITE"};

enum Page : uint8_t { PAGE_BOOT, PAGE_HOME, PAGE_WIDGETS, PAGE_DEVICE };
Page g_page = PAGE_BOOT;
Page g_pending_page = PAGE_BOOT;
bool g_page_change_pending = false;
uint32_t g_last_page_change_ms = 0;
lv_obj_t *g_screen = nullptr;
lv_timer_t *g_page_timer = nullptr;
bool g_ready = false;

// Page-specific objects. They are recreated whenever a page is opened.
lv_obj_t *g_boot_bar = nullptr;
lv_obj_t *g_boot_status = nullptr;
lv_obj_t *g_home_swatch = nullptr;
lv_obj_t *g_home_color_label = nullptr;
lv_obj_t *g_home_dot = nullptr;
lv_obj_t *g_home_bar = nullptr;
lv_obj_t *g_widget_slider = nullptr;
lv_obj_t *g_widget_slider_value = nullptr;
lv_obj_t *g_widget_tap_label = nullptr;
lv_obj_t *g_widget_touch_label = nullptr;
uint32_t g_widget_taps = 0;
uint32_t g_touch_label_ms = 0;
lv_obj_t *g_device_hint = nullptr;
lv_obj_t *g_device_switches[3] = {nullptr, nullptr, nullptr};
lv_obj_t *g_device_status_labels[3] = {nullptr, nullptr, nullptr};
const char *const kDeviceNames[] = {"WiFi", "BLE", "SD CARD"};
bool g_device_enabled[3] = {false, false, false};
int g_widget_value = 50;

struct HomeState {
  int dot_x = 48;
  int dot_step = 3;
  uint32_t elapsed_ms = 0;
  uint8_t color_index = 0;
};
HomeState g_home;


void show_page(Page page);

void request_page(Page page) {
  if (page == PAGE_BOOT || page == g_page) return;
  const uint32_t now = millis();
  if (now - g_last_page_change_ms < kPageChangeGuardMs) return;
  g_last_page_change_ms = now;
  g_pending_page = page;
  g_page_change_pending = true;
}

void nav_event(lv_event_t *event) {
  request_page(static_cast<Page>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
}

void screen_touch_event(lv_event_t *event) {
  // Only background touches navigate; slider/button/switch events stay local.
  if (lv_event_get_target(event) != g_screen || g_page == PAGE_BOOT) return;
  lv_indev_t *input = lv_indev_get_act();
  if (input == nullptr) return;
  const lv_dir_t direction = lv_indev_get_gesture_dir(input);
  if (direction == LV_DIR_LEFT || direction == LV_DIR_RIGHT) {
    const Page next = direction == LV_DIR_LEFT
        ? (g_page == PAGE_DEVICE ? PAGE_HOME : static_cast<Page>(g_page + 1))
        : (g_page == PAGE_HOME ? PAGE_DEVICE : static_cast<Page>(g_page - 1));
    request_page(next);  // Applied after lv_timer_handler, never delete in event.
  }
}

void widget_tap_event(lv_event_t * /*event*/) {
  char text[32];
  snprintf(text, sizeof(text), "TAP: %lu",
           static_cast<unsigned long>(++g_widget_taps));
  lv_label_set_text(g_widget_tap_label, text);
  lv_obj_center(g_widget_tap_label);
}

void update_widget_touch() {
  if (g_page != PAGE_WIDGETS || g_widget_touch_label == nullptr) return;
  const uint32_t now = millis();
  if (now - g_touch_label_ms < kTouchLabelPeriodMs) return;
  g_touch_label_ms = now;
  const TouchStatus &status = touch_get_status();
  const TouchSample &sample = touch_get_latest();
  char text[56];
  if (status.online) {
    snprintf(text, sizeof(text), "X: %u   Y: %u\n%s",
             sample.x, sample.y, sample.pressed ? "DOWN" : "UP");
  } else {
    snprintf(text, sizeof(text), "X: ---   Y: ---\nTP OFFLINE");
  }
  if (strcmp(lv_label_get_text(g_widget_touch_label), text) != 0) {
    lv_label_set_text(g_widget_touch_label, text);
  }
}

void widget_slider_event(lv_event_t *event) {
  g_widget_value = lv_slider_get_value(lv_event_get_target(event));
  char text[32];
  snprintf(text, sizeof(text), "Slide value: %d%%", g_widget_value);
  lv_label_set_text(g_widget_slider_value, text);
}

void device_switch_event(lv_event_t *event) {
  const uintptr_t id = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  if (id >= 3) return;
  g_device_enabled[id] = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
  lv_label_set_text(g_device_status_labels[id],
                    g_device_enabled[id] ? "Demo / ON" : "Demo / OFF");
  char text[80];
  snprintf(text, sizeof(text), "%s demo: %s\nNo hardware control",
           kDeviceNames[id], g_device_enabled[id] ? "ON" : "OFF");
  lv_label_set_text(g_device_hint, text);
  Serial.printf("[UI] %s demo switch=%u\n", kDeviceNames[id],
                g_device_enabled[id] ? 1U : 0U);
}


void set_label_color(lv_obj_t *label, lv_color_t color) {
  lv_obj_set_style_text_color(label, color, 0);
}


lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int width, int height) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, height);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, kCardColor, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

void create_nav_bar(lv_obj_t *screen) {
  const Page pages[] = {PAGE_HOME, PAGE_WIDGETS, PAGE_DEVICE};
  for (int i = 0; i < kNavItemCount; ++i) {
    lv_obj_t *button = lv_btn_create(screen);
    lv_obj_remove_style_all(button);
    const int inset = (kNavTouchSize - kNavDotSize) / 2;
    lv_obj_set_pos(button, kNavDotX[i] - inset, kNavDotY - inset);
    lv_obj_set_size(button, kNavTouchSize, kNavTouchSize);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, kAccentColor, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                              LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(button, nav_event, LV_EVENT_CLICKED,
                       reinterpret_cast<void *>(static_cast<uintptr_t>(pages[i])));
    lv_obj_t *dot = lv_obj_create(button);
    lv_obj_remove_style_all(dot);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(dot, inset, inset);
    lv_obj_set_size(dot, kNavDotSize, kNavDotSize);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot,
                              pages[i] == g_page ? kAccentColor : kMutedColor,
                              0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  }
}

void home_apply_color() {
  const lv_color_t color = kHomeColors[g_home.color_index];
  lv_obj_set_style_bg_color(g_home_swatch, color, 0);
  lv_label_set_text(g_home_color_label, kHomeColorNames[g_home.color_index]);
  set_label_color(g_home_color_label,
                  g_home.color_index == 4 ? lv_color_white() : kTextColor);
  lv_obj_set_style_bg_color(g_home_dot, color, 0);
}

void home_timer_cb(lv_timer_t *timer) {
  (void)timer;
  g_home.dot_x += g_home.dot_step;
  if (g_home.dot_x <= 48 || g_home.dot_x >= 284) {
    g_home.dot_step = -g_home.dot_step;
    g_home.dot_x += g_home.dot_step;
  }
  lv_obj_set_x(g_home_dot, g_home.dot_x);
  lv_bar_set_value(g_home_bar, ((g_home.dot_x - 48) * 100) / 236, LV_ANIM_OFF);

  g_home.elapsed_ms += kHomeTickMs;
  if (g_home.elapsed_ms >= kColorPeriodMs) {
    g_home.elapsed_ms = 0;
    g_home.color_index = static_cast<uint8_t>(
        (g_home.color_index + 1) %
        (sizeof(kHomeColors) / sizeof(kHomeColors[0])));
    home_apply_color();
  }
}

void create_boot_page(lv_obj_t *screen) {
  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "HXESP3636AI0180DIC-A");
  set_label_color(title, kTextColor);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 58);

  lv_obj_t *subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "DISPLAY UI DEMO");
  set_label_color(subtitle, kAccentColor);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 86);

  lv_obj_t *info = make_card(screen, 48, 126, 264, 108);
  lv_obj_t *label = lv_label_create(info);
  lv_label_set_text(label, "NV3003A   |   360 x 360\nQSPI 10 MHz\nLVGL");
  set_label_color(label, kMutedColor);
  lv_obj_center(label);

  g_boot_status = lv_label_create(screen);
  lv_label_set_text(g_boot_status, "Starting display...");
  set_label_color(g_boot_status, kMutedColor);
  lv_obj_align(g_boot_status, LV_ALIGN_CENTER, 0, 78);

  g_boot_bar = lv_bar_create(screen);
  lv_obj_set_size(g_boot_bar, 220, 12);
  lv_obj_align(g_boot_bar, LV_ALIGN_CENTER, 0, 106);
  lv_bar_set_range(g_boot_bar, 0, 100);
  lv_bar_set_value(g_boot_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_boot_bar, LV_COLOR_MAKE(0xDF, 0xE6, 0xEF), 0);
  lv_obj_set_style_bg_color(g_boot_bar, kAccentColor, LV_PART_INDICATOR);
}

void boot_timer_cb(lv_timer_t *timer) {
  static uint32_t elapsed = 0;
  (void)timer;
  elapsed += 50;
  lv_bar_set_value(g_boot_bar, (elapsed * 100) / kBootDurationMs, LV_ANIM_OFF);
  if (elapsed < 900) lv_label_set_text(g_boot_status, "Starting display...");
  else if (elapsed < 1700) lv_label_set_text(g_boot_status, "Checking LVGL...");
  else lv_label_set_text(g_boot_status, "Ready");
  if (elapsed >= kBootDurationMs) {
    elapsed = 0;
    // Delete the boot timer before replacing the boot screen. Otherwise the
    // next timer tick would access the deleted boot labels.
    lv_timer_del(timer);
    g_page_timer = nullptr;
    show_page(PAGE_HOME);
  }
}

void create_home_page(lv_obj_t *screen) {
  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "DISPLAY DEMO");
  set_label_color(title, kTextColor);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "COLOR  +  ANIMATION");
  set_label_color(subtitle, kMutedColor);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 34);

  const lv_color_t corner_colors[] = {
      LV_COLOR_MAKE(0xF0, 0x50, 0x55), LV_COLOR_MAKE(0x55, 0xC9, 0x8A),
      LV_COLOR_MAKE(0x55, 0x8D, 0xE8), LV_COLOR_MAKE(0xF0, 0xC8, 0x65)};
  const int positions[][2] = {{52, 68}, {250, 68}, {52, 238}, {250, 238}};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *circle = lv_obj_create(screen);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 58, 58);
    lv_obj_set_pos(circle, positions[i][0], positions[i][1]);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(circle, corner_colors[i], 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  }

  g_home_swatch = make_card(screen, 84, 132, 192, 48);
  g_home_color_label = lv_label_create(g_home_swatch);
  lv_obj_center(g_home_color_label);
  g_home_dot = lv_obj_create(screen);
  lv_obj_remove_style_all(g_home_dot);
  lv_obj_set_size(g_home_dot, 24, 24);
  lv_obj_set_pos(g_home_dot, 48, 188);
  lv_obj_set_style_bg_opa(g_home_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_home_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_clear_flag(g_home_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  g_home_bar = lv_bar_create(screen);
  lv_obj_set_size(g_home_bar, 236, 10);
  lv_obj_set_pos(g_home_bar, 48, 218);
  lv_bar_set_range(g_home_bar, 0, 100);
  lv_bar_set_value(g_home_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_home_bar, LV_COLOR_MAKE(0xDF, 0xE6, 0xEF), 0);
  lv_obj_set_style_bg_color(g_home_bar, kAccentColor, LV_PART_INDICATOR);

  g_home = HomeState{};
  home_apply_color();
  g_page_timer = lv_timer_create(home_timer_cb, kHomeTickMs, nullptr);
  create_nav_bar(screen);
}

void create_widgets_page(lv_obj_t *screen) {
  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "LVGL WIDGETS");
  set_label_color(title, kTextColor);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *card = make_card(screen, 64, 64, 232, 78);
  lv_obj_t *info = lv_label_create(card);
  lv_label_set_text(info, "TOUCH COORDINATES");
  set_label_color(info, kMutedColor);
  lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 8);
  g_widget_touch_label = lv_label_create(card);
  lv_obj_set_pos(g_widget_touch_label, 8, 30);
  lv_obj_set_width(g_widget_touch_label, 216);
  lv_obj_set_style_text_align(g_widget_touch_label, LV_TEXT_ALIGN_CENTER, 0);
  set_label_color(g_widget_touch_label, kTextColor);
  lv_label_set_text(g_widget_touch_label, "X: ---   Y: ---\nUP");
  g_touch_label_ms = millis() - kTouchLabelPeriodMs;

  lv_obj_t *tap = lv_btn_create(screen);
  lv_obj_set_pos(tap, 100, 154);
  lv_obj_set_size(tap, 160, 44);
  lv_obj_clear_flag(tap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                         LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_radius(tap, 14, 0);
  lv_obj_set_style_bg_color(tap, kAccentColor, 0);
  g_widget_tap_label = lv_label_create(tap);
  char count[32];
  snprintf(count, sizeof(count), "TAP: %lu",
           static_cast<unsigned long>(g_widget_taps));
  lv_label_set_text(g_widget_tap_label, count);
  lv_obj_center(g_widget_tap_label);
  lv_obj_add_event_cb(tap, widget_tap_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *slider_title = lv_label_create(screen);
  lv_label_set_text(slider_title, "TOUCH SLIDER");
  set_label_color(slider_title, kMutedColor);
  lv_obj_align(slider_title, LV_ALIGN_TOP_MID, 0, 208);

  g_widget_slider = lv_slider_create(screen);
  lv_obj_set_pos(g_widget_slider, kWidgetSliderX, kWidgetSliderY);
  lv_obj_set_size(g_widget_slider, kWidgetSliderWidth, kWidgetSliderHeight);
  lv_slider_set_range(g_widget_slider, 0, 100);
  lv_slider_set_value(g_widget_slider, g_widget_value, LV_ANIM_OFF);
  lv_obj_set_ext_click_area(g_widget_slider, 12);
  lv_obj_clear_flag(g_widget_slider, LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                                   LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_bg_color(g_widget_slider, LV_COLOR_MAKE(0xDF, 0xE6, 0xEF),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_widget_slider, kAccentColor, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_widget_slider, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_radius(g_widget_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(g_widget_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_widget_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);

  g_widget_slider_value = lv_label_create(screen);
  char value[32];
  snprintf(value, sizeof(value), "Slide value: %d%%", g_widget_value);
  lv_label_set_text(g_widget_slider_value, value);
  set_label_color(g_widget_slider_value, kTextColor);
  lv_obj_align(g_widget_slider_value, LV_ALIGN_TOP_MID, 0, 256);
  lv_obj_add_event_cb(g_widget_slider, widget_slider_event,
                      LV_EVENT_VALUE_CHANGED, nullptr);

  create_nav_bar(screen);
}

void create_device_card(lv_obj_t *screen, int y, uintptr_t id) {
  lv_obj_t *card = make_card(screen, 34, y, 292, 58);
  lv_obj_t *name_label = lv_label_create(card);
  lv_label_set_text(name_label, kDeviceNames[id]);
  set_label_color(name_label, kTextColor);
  lv_obj_set_pos(name_label, 14, 8);
  lv_obj_t *status_label = lv_label_create(card);
  lv_label_set_text(status_label, g_device_enabled[id] ? "Demo / ON" : "Demo / OFF");
  set_label_color(status_label, kMutedColor);
  lv_obj_set_pos(status_label, 14, 30);
  lv_obj_t *switch_control = lv_switch_create(card);
  lv_obj_clear_flag(switch_control, LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                                    LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_ext_click_area(switch_control, 8);
  lv_obj_set_size(switch_control, 50, 28);
  lv_obj_set_pos(switch_control, 226, 14);
  lv_obj_set_style_bg_color(switch_control, LV_COLOR_MAKE(0xD8, 0xE0, 0xEA),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(switch_control, kAccentColor,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(switch_control, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_color(switch_control, lv_color_white(),
                            LV_PART_KNOB | LV_STATE_CHECKED);
  g_device_switches[id] = switch_control;
  g_device_status_labels[id] = status_label;
  if (g_device_enabled[id]) lv_obj_add_state(switch_control, LV_STATE_CHECKED);
  lv_obj_add_event_cb(switch_control, device_switch_event, LV_EVENT_VALUE_CHANGED,
                      reinterpret_cast<void *>(id));
}

void create_device_page(lv_obj_t *screen) {
  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "DEVICE STATUS");
  set_label_color(title, kTextColor);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
  create_device_card(screen, 54, 0);
  create_device_card(screen, 122, 1);
  create_device_card(screen, 190, 2);
  g_device_hint = lv_label_create(screen);
  lv_obj_set_width(g_device_hint, 264);
  lv_obj_set_style_text_align(g_device_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(g_device_hint, "Demo mode\nNo hardware control");
  set_label_color(g_device_hint, kMutedColor);
  lv_obj_align(g_device_hint, LV_ALIGN_CENTER, 0, 102);
  create_nav_bar(screen);
}

void show_page(Page page) {
  // Release old input targets before deleting a screen; do not pass a held
  // gesture through to a control at the same position on the next screen.
  lvgl_port_wait_touch_release();
  if (g_page_timer != nullptr) {
    lv_timer_del(g_page_timer);
    g_page_timer = nullptr;
  }
  lv_obj_t *old_screen = g_screen;
  g_page = page;
  g_boot_bar = nullptr;
  g_boot_status = nullptr;
  g_home_swatch = nullptr;
  g_home_color_label = nullptr;
  g_home_dot = nullptr;
  g_home_bar = nullptr;
  g_widget_slider = nullptr;
  g_widget_slider_value = nullptr;
  g_widget_tap_label = nullptr;
  g_widget_touch_label = nullptr;
  g_device_hint = nullptr;
  g_device_switches[0] = nullptr;
  g_device_switches[1] = nullptr;
  g_device_switches[2] = nullptr;
  for (int i = 0; i < 3; ++i) g_device_status_labels[i] = nullptr;

  g_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(g_screen);
  lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(g_screen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(g_screen, screen_touch_event, LV_EVENT_GESTURE, nullptr);
  lv_obj_set_style_bg_color(g_screen, kBgColor, 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  if (page == PAGE_BOOT) create_boot_page(g_screen);
  else if (page == PAGE_HOME) create_home_page(g_screen);
  else if (page == PAGE_WIDGETS) create_widgets_page(g_screen);
  else create_device_page(g_screen);
  lv_scr_load(g_screen);
  if (old_screen != nullptr) lv_obj_del(old_screen);

  if (page == PAGE_BOOT) g_page_timer = lv_timer_create(boot_timer_cb, 50, nullptr);
  Serial.printf("[UI] Page=%u\n", static_cast<unsigned>(page));
}




}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  if (display_panel_init() != ESP_OK) {
    display_set_backlight(0);
    return;
  }
  touch_init();  // Offline TP retries automatically while the UI stays available.
  if (!lvgl_port_init()) {
    display_set_backlight(0);
    return;
  }
  show_page(PAGE_BOOT);
  display_set_backlight(100);
  g_ready = true;
  Serial.println("Swipe background or tap bottom dots; serial 1/2/3=pages, S=TP status");
}

void loop() {
  if (!g_ready) {
    delay(1000);
    return;
  }
  touch_poll();
  for (int count = 0; count < 8 && Serial.available() > 0; ++count) {
    switch (Serial.read()) {
      case '1': request_page(PAGE_HOME); break;
      case '2': request_page(PAGE_WIDGETS); break;
      case '3': request_page(PAGE_DEVICE); break;
      case 's':
      case 'S': {
        const TouchStatus &status = touch_get_status();
        const TouchSample &sample = touch_get_latest();
        Serial.printf("[TP] online=%u raw=(%u,%u) xy=(%u,%u) down=%u io=%lu invalid=%lu queue=%lu\n",
                      status.online ? 1U : 0U, sample.raw_x, sample.raw_y,
                      sample.x, sample.y, sample.pressed ? 1U : 0U,
                      static_cast<unsigned long>(status.io_errors),
                      static_cast<unsigned long>(status.invalid_frames),
                      static_cast<unsigned long>(status.queue_overflows));
        break;
      }
      default: break;
    }
  }
  lvgl_port_tick();
  lv_timer_handler();
  if (g_page_change_pending) {
    const Page target = g_pending_page;
    g_page_change_pending = false;
    show_page(target);
  }
  update_widget_touch();
  delay(2);
}
