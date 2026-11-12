# 纯色 display_demo 保留源码用于硬件排查，但不与 LVGL 同时编译运行。
idf_component_register(SRCS "main.c"
                      INCLUDE_DIRS "."
                      REQUIRES display lvgl_port ui_demo touch)
