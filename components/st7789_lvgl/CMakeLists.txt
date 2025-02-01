idf_component_register(
        SRCS            "st7789_driver.c"
                        "lv_port.c"
                        "lv_port_indev.c"
                        "font.c"
        INCLUDE_DIRS    "."
        REQUIRES        esp_lcd
                        lvgl
                        esp_partition
                        ui
)