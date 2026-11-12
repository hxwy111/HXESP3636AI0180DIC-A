#include <stdint.h>
#include <stdio.h>

#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUZZER_GPIO         GPIO_NUM_48
#define BUZZER_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER   LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_DUTY_RES     LEDC_TIMER_10_BIT
#define BUZZER_DUTY         ((1U << BUZZER_DUTY_RES) / 2U)

static void buzzer_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    const ledc_channel_config_t channel_config = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

static void buzzer_stop(void)
{
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));
}

static void buzzer_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    if (frequency_hz == 0) {
        buzzer_stop();
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }

    ESP_ERROR_CHECK(ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, frequency_hz));
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_stop();
}

void app_main(void)
{
    static const uint16_t melody[] = {262, 294, 330, 349, 392, 440, 494, 523};

    buzzer_init();
    buzzer_stop();
    printf("无源蜂鸣器测试程序启动\n");

    while (1) {
        for (size_t i = 0; i < sizeof(melody) / sizeof(melody[0]); ++i) {
            buzzer_tone(melody[i], 500);
        }
        printf("一轮音阶测试完成\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
