idf_component_register(
    SRCS
        "screens/settings_screen.c"
        "assets/dev_logo.c"

    INCLUDE_DIRS
        "include"
        "include/screens"
        "include/widgets"
        "include/assets"

    REQUIRES
        lvgl

    PRIV_REQUIRES
        models
)
