/* NV3003A 面板驱动：复位、初始化命令、GRAM 窗口和 RGB565 数据。 */
#include "display_panel.h"
#include "display_bus.h"
#include "display_config.h"
#include "nv3003a_init.h"
#include "Arduino.h"

namespace {
uint8_t g_line[DISPLAY_WIDTH * 2] __attribute__((aligned(4)));
}

// 背光使用 PWM，并按安全上限限制实际占空比。
void display_set_backlight(uint8_t percent) {
  if (percent > 100) percent = 100;
  percent = static_cast<uint8_t>((static_cast<uint16_t>(percent) *
                                  DISPLAY_BACKLIGHT_LIMIT_PERCENT) / 100);
  analogWrite(DISPLAY_BL_GPIO, (static_cast<int>(percent) * 255) / 100);
}

// 设置列地址、行地址和后续写入起点。
esp_err_t display_set_window(int x0, int y0, int x1, int y1) {
  if (x0 < 0 || y0 < 0 || x1 >= DISPLAY_WIDTH || y1 >= DISPLAY_HEIGHT || x0 > x1 || y0 > y1) {
    return ESP_ERR_INVALID_ARG;
  }
  const uint8_t col[] = { static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0),
                          static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1) };
  const uint8_t row[] = { static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0),
                          static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1) };
  esp_err_t err = display_bus_write_command(0x2A, col, sizeof(col));
  if (err != ESP_OK) return err;
  err = display_bus_write_command(0x2B, row, sizeof(row));
  if (err != ESP_OK) return err;
  return ESP_OK;
}

// 执行硬件复位，然后发送面板专用初始化命令表。
esp_err_t display_panel_init() {
  esp_err_t err = display_bus_init();
  if (err != ESP_OK) return err;
  digitalWrite(DISPLAY_RST_GPIO, LOW);
  delay(10);
  digitalWrite(DISPLAY_RST_GPIO, HIGH);
  delay(120);
  for (size_t i = 0; i < kNv3003aInitCount; ++i) {
    const Nv3003aInitCmd &item = kNv3003aInit[i];
    err = display_bus_write_command(item.cmd, item.data, item.data_len);
    if (err != ESP_OK) {
      return err;
    }
    if (item.delay_ms != 0) delay(item.delay_ms);
  }
  return ESP_OK;
}

// 使用 RGB565 填充矩形区域。
esp_err_t display_fill_rect(int x, int y, int width, int height, uint16_t rgb565) {
  if (width <= 0 || height <= 0) return ESP_ERR_INVALID_ARG;
  esp_err_t err = display_set_window(x, y, x + width - 1, y + height - 1);
  if (err != ESP_OK) return err;
  const uint8_t hi = static_cast<uint8_t>(rgb565 >> 8);
  const uint8_t lo = static_cast<uint8_t>(rgb565);
  for (int i = 0; i < DISPLAY_WIDTH; i++) {
    g_line[i * 2] = hi;
    g_line[i * 2 + 1] = lo;
  }
  for (int row = 0; row < height; ++row) {
    // 每行单独设置窗口，避免新的 2Ch 命令改变多行窗口的写入起点。
    err = display_set_window(x, y + row, x + width - 1, y + row);
    if (err != ESP_OK) return err;
    err = display_bus_write_color(g_line, static_cast<size_t>(width) * 2);
    if (err != ESP_OK) return err;
  }
  return ESP_OK;
}

esp_err_t display_fill(uint16_t rgb565) {
  return display_fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, rgb565);
}
