/*
 * NV3003A + LVGL display animation and color test.
 * The test keeps the screen background white and cycles several RGB colors.
 */
#include "Arduino.h"
#include "display_panel.h"
#include "lvgl_port.h"

namespace {
bool g_display_ready = false;
constexpr uint32_t kAnimationPeriodMs = 30;
constexpr uint32_t kColorPeriodMs = 1200;

struct DemoState {
  lv_obj_t *moving_dot;
  lv_obj_t *progress;
  lv_obj_t *color_swatch;
  lv_obj_t *color_label;
  int dot_x;
  int dot_step;
  uint32_t elapsed_ms;
  uint8_t color_index;
};

DemoState g_demo = {};

const lv_color_t kTestColors[] = {
    LV_COLOR_MAKE(0xF8, 0x20, 0x20),
    LV_COLOR_MAKE(0x20, 0xC8, 0x40),
    LV_COLOR_MAKE(0x20, 0x60, 0xF0),
    LV_COLOR_MAKE(0xF0, 0xD0, 0x20),
    LV_COLOR_MAKE(0x10, 0x10, 0x10),
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
};
const char *const kColorNames[] = {
    "RED", "GREEN", "BLUE", "YELLOW", "BLACK", "WHITE",
};

void apply_active_color() {
  const lv_color_t color = kTestColors[g_demo.color_index];
  lv_obj_set_style_bg_color(g_demo.color_swatch, color, 0);
  lv_label_set_text(g_demo.color_label, kColorNames[g_demo.color_index]);
  lv_obj_set_style_text_color(
      g_demo.color_label,
      g_demo.color_index == 4 ? lv_color_white() : lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_color(g_demo.moving_dot, color, 0);
}

void demo_timer_cb(lv_timer_t *timer) {
  DemoState *state = static_cast<DemoState *>(timer->user_data);
  if (state == nullptr) return;

  state->dot_x += state->dot_step;
  if (state->dot_x <= 45 || state->dot_x >= 283) {
    state->dot_step = -state->dot_step;
    state->dot_x += state->dot_step;
  }
  lv_obj_set_x(state->moving_dot, state->dot_x);
  const int progress = ((state->dot_x - 45) * 100) / (283 - 45);
  lv_bar_set_value(state->progress, progress, LV_ANIM_OFF);

  state->elapsed_ms += kAnimationPeriodMs;
  if (state->elapsed_ms >= kColorPeriodMs) {
    state->elapsed_ms = 0;
    state->color_index = static_cast<uint8_t>(
        (state->color_index + 1) % (sizeof(kTestColors) / sizeof(kTestColors[0])));
    apply_active_color();
  }
}

lv_obj_t *create_circle(lv_obj_t *parent, lv_color_t color, int x, int y) {
  lv_obj_t *circle = lv_obj_create(parent);
  lv_obj_remove_style_all(circle);
  lv_obj_set_size(circle, 58, 58);
  lv_obj_set_pos(circle, x, y);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(circle, color, 0);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
  return circle;
}

void create_demo_screen() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "DISPLAY COLOR TEST");
  lv_obj_set_style_text_color(title, lv_color_hex(0x202020), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t *subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "360 x 360  NV3003A + LVGL");
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x606060), 0);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 28);

  create_circle(screen, lv_color_hex(0xF00000), 52, 60);
  create_circle(screen, lv_color_hex(0x00C000), 250, 60);
  create_circle(screen, lv_color_hex(0x0000F0), 52, 242);
  create_circle(screen, lv_color_hex(0xF0D000), 250, 242);

  g_demo.color_swatch = lv_obj_create(screen);
  lv_obj_remove_style_all(g_demo.color_swatch);
  lv_obj_set_size(g_demo.color_swatch, 210, 52);
  lv_obj_set_pos(g_demo.color_swatch, 75, 128);
  lv_obj_set_style_bg_opa(g_demo.color_swatch, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_demo.color_swatch, 12, 0);

  g_demo.color_label = lv_label_create(g_demo.color_swatch);
  lv_obj_center(g_demo.color_label);

  g_demo.progress = lv_bar_create(screen);
  lv_obj_set_size(g_demo.progress, 238, 14);
  lv_obj_align(g_demo.progress, LV_ALIGN_CENTER, 0, 48);
  lv_bar_set_range(g_demo.progress, 0, 100);
  lv_bar_set_value(g_demo.progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_demo.progress, lv_color_hex(0xD8D8D8), 0);
  lv_obj_set_style_bg_color(g_demo.progress, lv_color_hex(0x2060E8), LV_PART_INDICATOR);

  g_demo.moving_dot = create_circle(screen, lv_color_hex(0xF82020), 45, 176);
  lv_obj_set_size(g_demo.moving_dot, 28, 28);

  lv_obj_t *hint = lv_label_create(screen);
  lv_label_set_text(hint, "moving dot / color cycle");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x505050), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

  g_demo.dot_x = 45;
  g_demo.dot_step = 3;
  g_demo.elapsed_ms = 0;
  g_demo.color_index = 0;
  apply_active_color();
  lv_timer_create(demo_timer_cb, kAnimationPeriodMs, &g_demo);
}
}  // namespace

void setup() {
  if (display_panel_init() != ESP_OK) {
    display_set_backlight(0);
    return;
  }
  lvgl_port_init();
  create_demo_screen();
  display_set_backlight(100);
  g_display_ready = true;
}

void loop() {
  if (!g_display_ready) {
    delay(1000);
    return;
  }
  lvgl_port_tick();
  lv_timer_handler();
  delay(5);
}
