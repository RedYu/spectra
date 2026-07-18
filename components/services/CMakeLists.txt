idf_component_register(
    SRCS
        "gui_service.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        models

    PRIV_REQUIRES
        drivers
        lvgl_port
        gui
        freertos
        nvs_flash
        fatfs
)
