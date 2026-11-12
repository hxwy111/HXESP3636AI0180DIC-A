#include <stdio.h>
#include "user_app.h"

/* ESP-IDF 程序入口：将具体的外设和业务初始化交给用户应用层。 */
void app_main(void)
{
  user_top_init();
}
