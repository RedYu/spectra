idf_component_register(
    SRCS
        "isotp_protocol.c"
        "isotp_session.c"
        "isotp_service.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        app_task_config
        can
        esp_common
        freertos
)
