idf_component_register(
    SRCS
        "xcp_protocol.c"
        "xcp_commands.c"
        "xcp_service.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        can
        esp_common

    PRIV_REQUIRES
        app_task_config
        esp_timer
        freertos
)
