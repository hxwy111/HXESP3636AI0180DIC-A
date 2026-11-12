/* 屏幕分辨率、GPIO、总线频率等板级配置集中在此，便于后续移植到 ESP-IDF。 */
#pragma once
#include <stdint.h>

// Board nets from HXESP3636AI0180DIC-A schematic.
static constexpr int DISPLAY_WIDTH = 360;
static constexpr int DISPLAY_HEIGHT = 360;
static constexpr int DISPLAY_SCLK_GPIO = 38; // LCD_SCL
static constexpr int DISPLAY_DATA0_GPIO = 4;  // QSPI_S0
static constexpr int DISPLAY_DATA1_GPIO = 5;  // QSPI_S1
static constexpr int DISPLAY_DATA2_GPIO = 6;  // QSPI_S2
static constexpr int DISPLAY_DATA3_GPIO = 7;  // QSPI_S3
static constexpr int DISPLAY_CS_GPIO = 12;
static constexpr int DISPLAY_DC_GPIO = 9;   // LCD_RS
static constexpr int DISPLAY_RST_GPIO = 39;
static constexpr int DISPLAY_BL_GPIO = 10;  // BL_PWM
static constexpr uint32_t DISPLAY_SPI_HZ = 10UL * 1000UL * 1000UL;
static constexpr bool DISPLAY_RGB_ORDER = true;
// QSPI requires the panel's IM0=1, IM2=0 straps and all four D0..D3 nets.
// Verify H1 pin 9 (D3), pin 11 (IM0), and pin 13 (GND) in the schematic.
static constexpr bool DISPLAY_USE_QSPI = true;
// 背光占空比上限，避免连续点亮时增加电源负载。
static constexpr uint8_t DISPLAY_BACKLIGHT_LIMIT_PERCENT = 50;
