#ifndef ADC_BSP_H
#define ADC_BSP_H

/* 读取 ADC 原始值及分压还原后的系统电压。 */
void adc_get_value(float *value,int *data);
/* 初始化 ADC1 通道 3和可选的曲线拟合校准方案。 */
void adc_bsp_init(void);
/* ADC 周期采样与打印示例任务。 */
void adc_example(void* parmeter);
#endif
