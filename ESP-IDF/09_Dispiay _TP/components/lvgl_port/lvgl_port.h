#pragma once

#include "esp_err.h"

/* 界面创建回调在 LVGL 专属任务中执行，可安全创建对象和 LVGL 定时器。 */
typedef esp_err_t (*lvgl_port_ui_create_cb_t)(void);

/*
 * 注册 360x360 显示设备，申请双 DMA 缓冲区并启动唯一的 LVGL 任务。
 * 调用前必须先初始化 display_panel 和 display_backlight。
 * 所有后续 LVGL 对象操作放在此任务的 LVGL 定时器/事件回调中；
 * 其他任务应通过消息队列传入业务数据，不能直接调用 LVGL 或写屏。
 * 返回值表示适配层启动结果，界面创建/首帧刷新结果由任务日志报告。
 */
esp_err_t lvgl_port_start(lvgl_port_ui_create_cb_t create_ui);
