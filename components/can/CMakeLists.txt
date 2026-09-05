idf_component_register(
    SRCS
        "can_frame.c"
        "can_router.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common

    PRIV_REQUIRES
        app_task_config
        drivers
        services
)
