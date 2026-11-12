idf_component_register(
    SRCS "display_bus.c" "display_panel.c" "display_backlight.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_ledc
)
