idf_component_register(
    SRCS
        "gui_service.c"
        "storage_service.c"
        "settings_service.c"
        "system_service.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        models

    PRIV_REQUIRES
        app
        gui
        esp_app_format
        drivers
        lvgl_port
        freertos
        nvs_flash
        fatfs
        spiffs
        cjson
)

spiffs_create_partition_image(
    storage
    ${PROJECT_DIR}/spiffs_data
    FLASH_IN_PROJECT
)
