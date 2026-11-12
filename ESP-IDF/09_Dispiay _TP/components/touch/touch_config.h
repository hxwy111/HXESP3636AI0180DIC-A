#pragma once

/* Same board wiring and orientation as Arduino/10_Dispiay_TP. */
#define TOUCH_SDA_GPIO                  15
#define TOUCH_SCL_GPIO                  14
#define TOUCH_INT_GPIO                  11
#define TOUCH_RST_GPIO                  40
#define TOUCH_I2C_PORT                  I2C_NUM_0
/* This example creates I2C0. Set to 1 ONLY if RTC/IMU already created it
 * on GPIO15/14 before touch_init(), using the same ESP-IDF master driver. */
#define TOUCH_USE_EXISTING_I2C_BUS      0
#define TOUCH_I2C_ADDRESS               0x15
#define TOUCH_I2C_HZ                    100000
#define TOUCH_I2C_TIMEOUT_MS            12

/* Inclusive raw maxima; do not use an observed partial range as calibration. */
#define TOUCH_RAW_X_MAX                 359
#define TOUCH_RAW_Y_MAX                 359
#define TOUCH_SWAP_XY                   0
#define TOUCH_REVERSE_X                 1  /* Confirmed by the Arduino board test. */
#define TOUCH_REVERSE_Y                 0
#define TOUCH_ACTIVE_POLL_MS            8
#define TOUCH_IDLE_POLL_MS              50
#define TOUCH_RETRY_MS                  2000
#define TOUCH_MAX_FAILURES              4
#define TOUCH_TRY_DISABLE_AUTO_SLEEP    1
