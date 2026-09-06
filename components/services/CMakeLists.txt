idf_component_register(
    SRCS
        "crash_dump_service.c"
        "logging_service.c"
        "gui_service.c"
        "storage_service.c"
        "storage_sd_service.c"
        "settings_service.c"
        "system_service.c"
        "usb_network_service.c"
        "web_service.c"
        "network_service.c"
        "wifi_service.c"
        "wifi_credentials_service.c"
        "internet_service.c"
        "mdns_service.c"
        "power_service.c"
        "web_content_service.c"
        "shutdown_service.c"
        "battery_service.c"
        "buzzer_service.c"
        "can_service.c"
        "can_fd_service.c"
        "can_twai_frame_adapter.c"
        "can_mcp2518fd_frame_adapter.c"
        "web_can_stream_service.c"
        "can_logger_service.c"

        "web_api/web_api_common.c"
        "web_api/web_system_api.c"
        "web_api/web_power_api.c"
        "web_api/web_settings_api.c"
        "web_api/web_files_api.c"
        "web_api/web_network_api.c"
        "web_api/web_can_protocol.c"

    INCLUDE_DIRS
        "include"

        "web_api/include"

    REQUIRES
        models
        can
        can_monitor

    PRIV_REQUIRES
        app_task_config
        app
        gui
        drivers
        lvgl_port

        freertos
        driver
        esp_driver_tsens

        esp_app_format
        esp_system
        esp_hw_support
        spi_flash
        esp_psram
        esp_timer

        esp_event
        esp_netif
        esp_wifi
        lwip

        nvs_flash
        fatfs
        spiffs
        cjson

        esp_tinyusb
        dns_server

        esp_http_server
        esp_http_client
        mbedtls

        espcoredump
        mdns
)

spiffs_create_partition_image(
    storage
    ${PROJECT_DIR}/spiffs_data
    FLASH_IN_PROJECT
)
