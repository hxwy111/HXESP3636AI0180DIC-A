#include "display_backlight.h"
#include "display_config.h"

#include "driver/ledc.h"

#define BACKLIGHT_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_TIMER      LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL    LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_RESOLUTION LEDC_TIMER_8_BIT
#define BACKLIGHT_LEDC_FREQUENCY  5000

/* 配置 LEDC 定时器和 GPIO10 背光输出通道，初始占空比为 0。 */
esp_err_t display_backlight_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_LEDC_RESOLUTION,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = DISPLAY_BACKLIGHT_GPIO,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel_config);
}

esp_err_t display_backlight_set(uint8_t percent)
{
    /* 先限制用户输入，再映射到项目设定的最大安全亮度。 */
    if (percent > 100) {
        percent = 100;
    }
    if (percent > DISPLAY_BACKLIGHT_MAX_PERCENT) {
        percent = DISPLAY_BACKLIGHT_MAX_PERCENT;
    }

    /* 8 位 PWM 的满量程为 255。 */
    const uint32_t max_duty = (1U << BACKLIGHT_LEDC_RESOLUTION) - 1U;
    const uint32_t duty = (max_duty * percent) / 100U;
    esp_err_t err = ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
}
