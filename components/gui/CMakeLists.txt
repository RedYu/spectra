idf_component_register(
    SRCS
        "screens/splash_screen.c"
        "screens/main_screen.c"
        "screens/settings_screen.c"

        "widgets/toolbar.c"

        "assets/dev_logo.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        lvgl

    PRIV_REQUIRES
        models
)

