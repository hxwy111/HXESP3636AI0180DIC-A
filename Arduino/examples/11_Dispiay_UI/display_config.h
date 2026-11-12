#pragma once
#include <stdint.h>

static constexpr int DISPLAY_WIDTH = 360;
static constexpr int DISPLAY_HEIGHT = 360;
static constexpr int DISPLAY_SCLK_GPIO = 38;
static constexpr int DISPLAY_DATA0_GPIO = 4;
static constexpr int DISPLAY_DATA1_GPIO = 5;
static constexpr int DISPLAY_DATA2_GPIO = 6;
static constexpr int DISPLAY_DATA3_GPIO = 7;
static constexpr int DISPLAY_CS_GPIO = 12;
static constexpr int DISPLAY_DC_GPIO = 9;
static constexpr int DISPLAY_RST_GPIO = 39;
static constexpr int DISPLAY_BL_GPIO = 10;
static constexpr uint32_t DISPLAY_SPI_HZ = 10UL * 1000UL * 1000UL;
static constexpr bool DISPLAY_RGB_ORDER = true;
static constexpr bool DISPLAY_USE_QSPI = true;
static constexpr uint8_t DISPLAY_BACKLIGHT_LIMIT_PERCENT = 50;
