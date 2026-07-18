idf_component_register(
    SRCS
        "lvgl_port.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        lvgl

    PRIV_REQUIRES
        drivers
        esp_timer
        freertos
)
