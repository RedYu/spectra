idf_component_register(
    SRCS
        "screen_manager.c"

        "screens/splash_screen.c"
        "screens/main_screen.c"
        "screens/settings_screen.c"

        "widgets/toolbar.c"
        "widgets/modal_dialog.c"
        "widgets/sd_card_modal.c"

        "assets/dev_logo.c"
        "assets/icons8_alarm_clock_32.c"
        "assets/icons8_micro_sd_32.c"
        "assets/icons8_settings_32.c"
        "assets/icons8_back_32.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        lvgl

    PRIV_REQUIRES
        models
        services
        drivers
)

