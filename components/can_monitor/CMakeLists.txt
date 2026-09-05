idf_component_register(
    SRCS
        "can_monitor_service.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        can
        esp_system

    PRIV_REQUIRES
        app_task_config
        freertos
        esp_timer
)
