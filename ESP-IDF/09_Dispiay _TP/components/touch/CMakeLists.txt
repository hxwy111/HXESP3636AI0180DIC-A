idf_component_register(SRCS "touch_driver.c"
                      INCLUDE_DIRS "."
                      PRIV_REQUIRES display esp_driver_i2c esp_driver_gpio esp_timer freertos log)
