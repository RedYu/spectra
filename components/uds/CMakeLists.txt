idf_component_register(
    SRCS
        "uds_protocol.c"
        "uds_requests.c"
        "uds_client.c"
        "uds_security_access.c"
        "uds_security_provider.c"
        "uds_download.c"
        "uds_ecu_profile.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        esp_timer
        isotp
)
