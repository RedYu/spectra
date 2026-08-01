idf_component_register(
    SRCS
        "logging_service.c"
        "gui_service.c"
        "storage_service.c"
        "storage_sd_service.c"
        "settings_service.c"
        "system_service.c"
        "usb_network_service.c"
        "web_service.c"

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
        esp_tinyusb
        esp_hw_support
        esp_netif
        lwip
        dns_server
        esp_http_server
)

spiffs_create_partition_image(
    storage
    ${PROJECT_DIR}/spiffs_data
    FLASH_IN_PROJECT
)
