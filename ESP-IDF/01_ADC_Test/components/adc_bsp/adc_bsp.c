#include <stdio.h>
#include "adc_bsp.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 启用 ADC 曲线拟合校准，提高原始采样值转换为电压时的精度。 */
#define ADC_Calibrate

#ifdef ADC_Calibrate
  /* ADC 校准方案句柄。 */
  static adc_cali_handle_t cali_handle;
#endif
  /* ADC1 单次采样单元句柄。 */
  static adc_oneshot_unit_handle_t adc1_handle;

/* 初始化 ADC1、校准方案以及 ADC1 通道 2。 */
void adc_bsp_init(void)
{
#ifdef ADC_Calibrate
  /* 使用 12 dB 衰减和 12 位分辨率创建曲线拟合校准方案。 */
  adc_cali_curve_fitting_config_t cali_config = 
  {
    .unit_id = ADC_UNIT_1,
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12, // 12 位 ADC 的满量程计数为 4096
  };
  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle));
#endif
  adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
  adc_oneshot_chan_cfg_t config = {
    .bitwidth = ADC_BITWIDTH_12,
    .atten = ADC_ATTEN_DB_12, // 12 dB 衰减用于测量较高的输入电压
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config));
}

/*
 * 读取 ADC1 通道 2：data 返回原始采样值，value 返回分压还原后的系统电压。
 * 硬件采样点采用 1:3 分压，因此 ADC 引脚电压需要乘以 3。
 */
void adc_get_value(float *value,int *data)
{
  int adcdata;
#ifdef ADC_Calibrate
  int vol = 0;
#endif
  esp_err_t err;
  err = adc_oneshot_read(adc1_handle,ADC_CHANNEL_2,&adcdata);
  if(err == ESP_OK)
  {
#ifdef ADC_Calibrate
    /* 将原始值转换为毫伏，再按硬件分压比例还原系统电压。 */
    adc_cali_raw_to_voltage(cali_handle,adcdata,&vol);
    *value = 0.001 * vol * 3;
#else
    /* 未启用校准时，按 12 位 ADC 满量程进行近似换算。 */
    *value = ((float)adcdata * 3.3/4096) * 3;
#endif
    *data = adcdata;
  }
  else
  {
    *value = 0;
    *data = 0;
  }
}
/* ADC 示例任务：每秒读取并打印一次原始值和系统电压。 */
void adc_example(void* parmeter)
{
  adc_bsp_init();
  int adcdata = 0;
  float _vol = 0;
  for(;;)
  {
    adc_get_value(&_vol,&adcdata);
    printf("adc value:%d,system voltage:%f\n",adcdata,_vol);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
