idf_component_register(SRCS "lvgl_port.c"
                      INCLUDE_DIRS "."
                      REQUIRES lvgl
                      PRIV_REQUIRES touch display esp_timer heap freertos log)
