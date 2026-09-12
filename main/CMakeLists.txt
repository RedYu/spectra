idf_component_register(
    SRCS
        "main.c"

    INCLUDE_DIRS
        "."

    PRIV_REQUIRES
        app
        can_monitor
        board
        drivers
        models
        lvgl_port
        gui
        isotp
        services
        freertos
        esp_event
        esp_netif
        nvs_flash
)
