idf_component_register(
    SRCS
        "crash_dump_service.c"
        "logging_service.c"
        "gui_service.c"
        "storage_service.c"
        "storage_sd_service.c"
        "storage_sd_benchmark.c"
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
        "can_transmit_service.c"
        "can_logger_binary_format.c"
        "time_service.c"

        "web_api/web_api_common.c"
        "web_api/web_system_api.c"
        "web_api/web_power_api.c"
        "web_api/web_settings_api.c"
        "web_api/web_files_api.c"
        "web_api/web_network_api.c"
        "web_api/web_can_protocol.c"
        "web_api/web_can_transmit_api.c"
        "web_api/web_can_filters_api.c"

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

if(NOT SPECTRA_TEST_BUILD)
    if(CONFIG_SPECTRA_WEB_CONTENT_GZIP)

        /*
         * Check whether Python dependencies required by the
         * web asset build script are available.
         */
        execute_process(
            COMMAND
                ${PYTHON}
                -c
                "import htmlmin, rcssmin, rjsmin"

            RESULT_VARIABLE
                WEB_PYTHON_DEPS_RESULT

            OUTPUT_QUIET
            ERROR_QUIET
        )

        /*
         * Install missing Python dependencies only when required.
         */
        if(NOT WEB_PYTHON_DEPS_RESULT EQUAL 0)
            message(
                STATUS
                "Installing Spectra web build Python dependencies..."
            )

            execute_process(
                COMMAND
                    ${PYTHON}
                    -m
                    pip
                    install
                    -r
                    ${PROJECT_DIR}/scripts/requirements.txt

                WORKING_DIRECTORY
                    ${PROJECT_DIR}

                RESULT_VARIABLE
                    WEB_REQUIREMENTS_RESULT
            )

            if(NOT WEB_REQUIREMENTS_RESULT EQUAL 0)
                message(
                    FATAL_ERROR
                    "Failed to install Spectra web build dependencies"
                )
            endif()
        endif()

        /*
         * Build minified and gzip-compressed web resources.
         */
        execute_process(
            COMMAND
                ${PYTHON}
                ${PROJECT_DIR}/scripts/build_web.py

            WORKING_DIRECTORY
                ${PROJECT_DIR}

            RESULT_VARIABLE
                WEB_CONTENT_BUILD_RESULT
        )

        if(NOT WEB_CONTENT_BUILD_RESULT EQUAL 0)
            message(
                FATAL_ERROR
                "Failed to build compressed Spectra web resources"
            )
        endif()
    endif()

    spiffs_create_partition_image(
        storage
        ${PROJECT_DIR}/spiffs_data
        FLASH_IN_PROJECT
    )
endif()
