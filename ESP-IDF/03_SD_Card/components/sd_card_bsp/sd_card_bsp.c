#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_card_bsp.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


//#define SDMMC_U
#define PIN_NUM_MISO  (gpio_num_t)42
#define PIN_NUM_MOSI  (gpio_num_t)1
#define PIN_NUM_CLK   (gpio_num_t)2
#define SDlist "/sd_card" // SD 卡在虚拟文件系统中的挂载目录
#ifndef SDMMC_U
#define PIN_NUM_CS    (gpio_num_t)41
#define SD_SPI SPI3_HOST
#endif


sdmmc_card_t *card = NULL; // SD 卡设备句柄


void SD_card_Init(void)
{
#ifdef SDMMC_U
  esp_vfs_fat_sdmmc_mount_config_t mount_config = 
  {
    .format_if_mount_failed = false,     // 挂载失败时不自动创建分区表和格式化 SD 卡
    .max_files = 5,                      // 允许同时打开的最大文件数
    .allocation_unit_size = 512,         // 文件系统分配单元大小，作用类似扇区大小
  };

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  //host.max_freq_khz = SDMMC_FREQ_HIGHSPEED; // 使用高速模式

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;           // 使用 SDMMC 单线模式
  slot_config.clk = PIN_NUM_CLK;
  slot_config.cmd = PIN_NUM_MOSI;
  slot_config.d0 = PIN_NUM_MISO;
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_fat_sdmmc_mount(SDlist, &host, &slot_config, &mount_config, &card));

  if(card != NULL)
  {
    sdmmc_card_print_info(stdout, card); // 打印 SD 卡信息
    printf("practical_size:%.2fG\n",(float)(card->csd.capacity)/2048/1024);// 容量单位：GB
  }
#endif

#ifndef SDMMC_U
  esp_vfs_fat_sdmmc_mount_config_t mount_config = 
  {
    .format_if_mount_failed = false,    // 挂载失败时创建分区表并格式化 SD 卡
    .max_files = 5,                    // 允许同时打开的最大文件数
    .allocation_unit_size = 512        // 文件系统分配单元大小，作用类似扇区大小
  };                                                                                                      
  spi_bus_config_t bus_cfg = 
  {
    .mosi_io_num = PIN_NUM_MOSI,
    .miso_io_num = PIN_NUM_MISO,
    .sclk_io_num = PIN_NUM_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 4000,   // SPI 单次传输的最大字节数
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_initialize(SD_SPI, &bus_cfg, SDSPI_DEFAULT_DMA));
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = PIN_NUM_CS;
  slot_config.host_id = SD_SPI;
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SD_SPI;
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_fat_sdspi_mount(SDlist, &host, &slot_config, &mount_config, &card)); 
  if(card != NULL)
  {
    sdmmc_card_print_info(stdout, card); // 打印 SD 卡信息
    printf("practical_size:%.2fG\n",(float)(card->csd.capacity)/2048/1024);// 容量单位：GB
  }
#endif
}
float sd_cadr_get_value(void)
{
  if(card != NULL)
  {
    return (float)(card->csd.capacity)/2048/1024; // 返回容量，单位：GB
  }
  else
  return 0;
}

/* 写入数据
path：文件路径
data：待写入的数据
*/
esp_err_t s_example_write_file(const char *path, char *data)
{
  esp_err_t err;
  if(card == NULL)
  {
    return ESP_ERR_NOT_FOUND;
  }
  err = sdmmc_get_status(card); // 首先检查 SD 卡是否存在且状态正常
  if(err != ESP_OK)
  {
    return err;
  }
  FILE *f = fopen(path, "w"); // 以写入方式打开指定路径的文件
  if(f == NULL)
  {
    printf("path:Write Wrong path\n");
    return ESP_ERR_NOT_FOUND;
  }
  fprintf(f,"%s", data); // 将数据写入文件
  fclose(f);
  return ESP_OK;
}
/*
读取数据
path：文件路径
*/
esp_err_t s_example_read_file(const char *path,uint8_t *pxbuf,uint32_t *outLen)
{
  esp_err_t err;
  if(card == NULL)
  {
    printf("path:card == NULL\n");
    return ESP_ERR_NOT_FOUND;
  }
  err = sdmmc_get_status(card); // 首先检查 SD 卡是否存在且状态正常
  if(err != ESP_OK)
  {
    printf("path:card == NO\n");
    return err;
  }
  FILE *f = fopen(path, "rb");
  if (f == NULL)
  {
    printf("path:Read Wrong path\n");
    return ESP_ERR_NOT_FOUND;
  }
  fseek(f, 0, SEEK_END);     // 将文件指针移动到文件末尾
  uint32_t unlen = ftell(f);
  //fgets(pxbuf, unlen, f); // 读取文本数据
  fseek(f, 0, SEEK_SET); // 将文件指针移回文件开头
  uint32_t poutLen = fread((void *)pxbuf,1,unlen,f);
  printf("pxlen: %ld,outLen: %ld\n",unlen,poutLen);
  *outLen = poutLen;
  fclose(f);
  return ESP_OK;
}
