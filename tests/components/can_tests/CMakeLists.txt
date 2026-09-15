idf_component_register(
    SRCS
        "test_can_transmit_sequence.c"
        "test_can_hardware_filters.c"
        "test_isotp_protocol.c"
        "test_isotp_session.c"
        "test_uds_protocol.c"
        "test_uds_security_access.c"
        "test_uds_security_provider.c"
        "test_uds_download.c"
        "test_uds_ecu_profile.c"
        "test_xcp_protocol.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        unity
        services
        can
        drivers
        isotp
        uds
        xcp
    
    WHOLE_ARCHIVE
)
