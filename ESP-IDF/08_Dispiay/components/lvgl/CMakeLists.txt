# 本地 LVGL 8.3.11，仅编译核心源码，不依赖 Arduino 或在线组件下载。
file(GLOB_RECURSE LVGL_SOURCES "src/*.c")
idf_component_register(SRCS ${LVGL_SOURCES}
                      INCLUDE_DIRS "."
                      REQUIRES esp_timer)
# 所有使用 LVGL 头文件的组件必须共享同一份配置。
target_compile_definitions(${COMPONENT_LIB} PUBLIC
                           LV_CONF_INCLUDE_SIMPLE=1 LV_KCONFIG_IGNORE=1)
