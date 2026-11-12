#ifndef USER_APP_H
#define USER_APP_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* 创建用户功能任务。 */
void user_app_init(void);
/* 应用顶层初始化入口。 */
void user_top_init(void);

#endif

