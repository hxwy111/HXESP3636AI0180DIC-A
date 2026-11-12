#include <stdio.h>
#include "user_app.h"

#include "sd_card_bsp.h"

void user_app_init(void);

void user_top_init(void)
{
  SD_card_Init();
  char write_sd_buffer[]="hello sd card!\n";
  uint8_t read_sd_buffer[256]={};
  uint32_t read_sd_len=0;

  esp_err_t ret=s_example_write_file("/sd_card/test.txt",write_sd_buffer);
  if(ret==ESP_OK)
  {
    printf("文件写入成功\n");
  }
  else{
    printf("文件写入失败：%s\n",esp_err_to_name(ret));
  }

  ret=s_example_read_file("/sd_card/test.txt",read_sd_buffer,&read_sd_len);

  if(ret==ESP_OK)
  {
    if(read_sd_len<sizeof(read_sd_buffer)){
      read_sd_buffer[read_sd_len]='\0';
    }else{
      read_sd_buffer[sizeof(read_sd_buffer)-1]='\0';
    }
  }
  printf("读取的数据是:%s\n",read_sd_buffer);
  printf("读取数据的长度是:%ld\n",read_sd_len);
}





