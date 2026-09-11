idf_component_register(
    SRCS
        "test_can_transmit_sequence.c"
        "test_can_hardware_filters.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        unity
        services
        can
        drivers
)
