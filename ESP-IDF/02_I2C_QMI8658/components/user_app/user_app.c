#include <stdio.h>
#include "user_app.h"

#include "i2c_bsp.h"
#include "qmi8658c.h"

void user_app_init(void);

/* 用户层总入口：先初始化底层总线，再创建功能任务。 */
void user_top_init(void)
{
  I2C_master_Init();    // 初始化 I2C 总线
    /* 扫描并打印当前总线上的设备地址。 */
  I2C_scan_devices();
  user_app_init();      // 初始化用户功能
}

/* 创建当前示例所需的 FreeRTOS 任务。 */
void user_app_init(void)
{
  //vTaskDelay(pdMS_TO_TICKS(5000));
  //xTaskCreate(example_rs485_task, "example_rs485_task", 3000, NULL, 2, &rs485_test);
  //xTaskCreate(example_hasswp_task, "example_hasswp_task", 6000,(void*)(&xmqttMess), 2, &hasswp_test);
  //xTaskCreate(qmi8658c_example, "qmi8658c_example", 3000, NULL, 2, NULL);
  //xTaskCreate(PCF85063_example, "PCF85063_example", 3000, NULL , 2, NULL);
  /* 启动 QMI8658C 数据采集示例：栈 3000 字节，优先级 2。 */
  xTaskCreate(qmi8658c_example, "qmi8658c_example", 3000, NULL , 2, NULL);
  //xTaskCreate(example_can_task_read, "example_can_task_read", 3000, NULL , 2, NULL);
}


