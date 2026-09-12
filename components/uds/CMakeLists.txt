idf_component_register(
    SRCS
        "uds_protocol.c"
        "uds_requests.c"
        "uds_client.c"
        "uds_security_access.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        esp_timer
        isotp
)
