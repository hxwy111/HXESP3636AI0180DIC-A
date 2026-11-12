/* NV3003A 屏幕初始化和颜色轮播示例。 */
#include "display_config.h"
#include "display_panel.h"
#include "Arduino.h"

namespace {
bool g_display_ready = false;

// 显示一种纯色并保持指定时间。
void show_color(uint16_t color, uint32_t hold_ms) {
  if (display_fill(color) == ESP_OK) {
    delay(hold_ms);
  }
}

// 显示红、绿、蓝、黄四色区域。
void show_color_blocks(uint32_t hold_ms) {
  const int half_width = DISPLAY_WIDTH / 2;
  const int half_height = DISPLAY_HEIGHT / 2;
  if (display_fill_rect(0, 0, half_width, half_height, 0xF800) != ESP_OK) return;
  if (display_fill_rect(half_width, 0, half_width, half_height, 0x07E0) != ESP_OK) return;
  if (display_fill_rect(0, half_height, half_width, half_height, 0x001F) != ESP_OK) return;
  if (display_fill_rect(half_width, half_height, half_width, half_height, 0xFFE0) != ESP_OK) return;
  delay(hold_ms);
}
}

void setup() {
  // 初始化 GPIO、QSPI 总线、硬件复位和 BOE 面板专用命令表。
  if (display_panel_init() != ESP_OK) {
    display_set_backlight(0);
    return;
  }

  display_set_backlight(100);
  g_display_ready = true;
}

void loop() {
  if (!g_display_ready) {
    delay(1000);
    return;
  }

  // 依次显示基础颜色。
  show_color(0xF800, 1000);  // 红色
  show_color(0x07E0, 1000);  // 绿色
  show_color(0x001F, 1000);  // 蓝色
  show_color(0xFFFF, 1000);  // 白色
  show_color(0x0000, 1000);  // 黑色

  // 显示四色块后重新开始下一轮。
  show_color_blocks(2000);
}
