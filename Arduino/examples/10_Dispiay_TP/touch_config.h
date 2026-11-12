#pragma once

#include <stdint.h>

// HXESP3636AI0180DIC-A schematic: U12 CST816D, 3.3 V.
// GPIO14/15 are shared with the RTC and IMU; initialize Wire only here.
static constexpr int TOUCH_SDA_GPIO = 15;
static constexpr int TOUCH_SCL_GPIO = 14;
static constexpr int TOUCH_INT_GPIO = 11;
static constexpr int TOUCH_RST_GPIO = 40;
static constexpr uint8_t TOUCH_I2C_ADDRESS = 0x15;  // Seven-bit address.
static constexpr uint32_t TOUCH_I2C_HZ = 100000;
static constexpr uint16_t TOUCH_I2C_TIMEOUT_MS = 12;

// Initial assumption, to be checked on the diagnostic page on real hardware.
// These are inclusive RAW maxima, not a resolution or an observed auto-range.
static constexpr uint16_t TOUCH_RAW_X_MAX = 359;
static constexpr uint16_t TOUCH_RAW_Y_MAX = 359;
static constexpr bool TOUCH_SWAP_XY = false;
static constexpr bool TOUCH_REVERSE_X = true;  // Board test: raw X is mirrored.
static constexpr bool TOUCH_REVERSE_Y = false;

static constexpr uint32_t TOUCH_ACTIVE_POLL_MS = 8;
static constexpr uint32_t TOUCH_IDLE_POLL_MS = 50;
static constexpr uint32_t TOUCH_RETRY_MS = 2000;
static constexpr uint8_t TOUCH_MAX_FAILURES = 4;

// CST8xxT family register 0xFE: attempt and verify, never assume support.
// If unsupported, keep IRQ-driven wakeup and poll only during an active touch.
static constexpr bool TOUCH_TRY_DISABLE_AUTO_SLEEP = true;
