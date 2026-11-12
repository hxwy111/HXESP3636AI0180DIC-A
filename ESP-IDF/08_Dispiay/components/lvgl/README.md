本组件使用 Arduino/libraries/lvgl 的 LVGL 8.3.11 核心源码和原始许可证。
为适配 ESP-IDF，新增本地 CMakeLists.txt 与 lv_conf.h；未修改 vendor 的 src。
不包含 Arduino 运行时，不依赖联网下载。配置以 lv_conf.h 为准，不使用 Kconfig。
