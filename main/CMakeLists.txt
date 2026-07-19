idf_component_register(
    SRCS
        "main.c"

    INCLUDE_DIRS
        "."

    PRIV_REQUIRES
        app
        board
        drivers
        models
        lvgl_port
        gui
        services
        freertos
)