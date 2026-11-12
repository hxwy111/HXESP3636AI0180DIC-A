#include <stdio.h>
#include "esp_wifi_bsp.h"
#include "esp_event.h" // Event
#include "nvs_flash.h" // NVS storage

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
//TaskHandle_t pxWIFIreadTask;
//
//QueueHandle_t WIFI_QueueHandle;

void espwifi_Init(void)
{
  nvs_flash_init();                    // Initialize default NVS storage
  esp_netif_init();                    // Initialize TCP/IP stack
  esp_event_loop_create_default();     // Create default event loop
  esp_netif_create_default_wifi_sta(); // Attach TCP/IP stack to default event loop
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Default config
  esp_wifi_init(&cfg);                                 // Initialize Wi-Fi
  esp_event_handler_instance_t Instance_WIFI_IP;
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &Instance_WIFI_IP);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &Instance_WIFI_IP);
  wifi_config_t wifi_config = {
      .sta = {
        .ssid = "nova6",
        .password = "asdfghjkl",
      },
  };
  esp_wifi_set_mode(WIFI_MODE_STA);               // Set mode to STA
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config); // Configure Wi-Fi
  esp_wifi_start();                               // Start Wi-Fi
  //WIFI_QueueHandle = xQueueCreate(30, sizeof(wifi_scan_config_t));  // Create a queue with 30 items, each item 20 bytes
  //printf("wifi_scan_config_t: %d\n", sizeof(wifi_scan_config_t));
}

static void event_handler(void *arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data)
{
    if(event_base == WIFI_EVENT &&
       event_id == WIFI_EVENT_STA_START)
    {
        printf("Wi-Fi STA 已启动，开始连接热点\n");
        esp_wifi_connect();
    }
    else if(event_base == WIFI_EVENT &&
            event_id == WIFI_EVENT_STA_CONNECTED)
    {
        printf("已关联热点，正在等待 DHCP 分配 IP\n");
    }
    else if(event_base == WIFI_EVENT &&
            event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;

        printf("Wi-Fi 断开，原因码：%d\n", event->reason);

        /* 断开后重新尝试连接。 */
        esp_wifi_connect();
    }
    else if(event_base == IP_EVENT &&
            event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        printf("获取 IP 成功：" IPSTR "\n",
               IP2STR(&event->ip_info.ip));
    }
}