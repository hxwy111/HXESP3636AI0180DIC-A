/*  Wi-Fi SoftAP 示例

   本示例代码属于公共领域，也可以选择采用 CC0 许可证。

   除非适用法律要求或另有书面约定，本软件按“原样”提供，
   不附带任何明示或暗示的保证或条件。
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

/* SoftAP 热点参数：名称、密码、信道以及允许同时接入的最大客户端数量。 */
#define EXAMPLE_ESP_WIFI_SSID      "waveshare_esp32"
#define EXAMPLE_ESP_WIFI_PASS      "wav123456"
#define EXAMPLE_ESP_WIFI_CHANNEL   1
#define EXAMPLE_MAX_STA_CONN       4

static const char *TAG = "wifi softAP";
/* 保存最近接入热点的客户端 MAC 地址。 */
uint8_t mac[6];

/* 统一处理客户端连接、断开以及 DHCP 分配 IP 地址等事件。 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        /* 客户端接入热点时，记录其 MAC 地址。 */
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        for(uint8_t i = 0; i<6;i++)
        mac[i] = event->mac[i];
    } 
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        /* 客户端主动断开或连接丢失。 */
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        printf("disconnect\n");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED)
    {
        /* DHCP 服务器为客户端分配地址后，打印该客户端的 MAC 和 IPv4 地址。 */
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        char ip[25];
        uint32_t pxip = event->ip_info.ip.addr;
        sprintf(ip, "%d.%d.%d.%d", (uint8_t)(pxip), (uint8_t)(pxip >> 8), (uint8_t)(pxip >> 16), (uint8_t)(pxip >> 24));
        printf("MAC: %d:%d:%d:%d:%d:%d  IP: %s\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],ip);
    }
}

/* 初始化网络接口、Wi-Fi 驱动和事件处理器，并启动 SoftAP 热点。 */
void wifi_init_softap(void)
{
    /* 创建 TCP/IP 协议栈、默认事件循环及 SoftAP 默认网络接口。 */
    ESP_ERROR_CHECK(esp_netif_init());
    //创建默认的系统事件循环。后续注册事件处理器时依赖该循环。
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 监听全部 Wi-Fi 事件，以便处理客户端连接和断开。 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    /* 监听 SoftAP 为客户端分配 IP 地址的事件。 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_AP_STAIPASSIGNED,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    /* 配置 WPA2-PSK 热点，并要求客户端使用受保护管理帧（PMF）。 */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

/* 程序入口：先初始化 Wi-Fi 配置依赖的 NVS，再启动 SoftAP。 */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    /* NVS 空间不足或数据版本不兼容时，擦除后重新初始化。 */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init_softap();
}
