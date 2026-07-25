#include "gui_service.h"

#include "screens/main_screen.h"
#include "screens/splash_screen.h"
#include "screens/settings_screen.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "screen_manager.h"

static const char *TAG = "gui_service";

#define GUI_TASK_STACK_SIZE 8192
#define GUI_TASK_PRIORITY   5

static esp_err_t gui_register_screens(void)
{
    static const screen_desc_t splash_descriptor = {
        .id = SCREEN_ID_SPLASH,
        .name = "Splash",

        .create = splash_screen_create,
        .on_show = splash_screen_on_show,
        .on_hide = splash_screen_on_hide,
        .destroy = splash_screen_destroy,
    };

    static const screen_desc_t main_descriptor = {
        .id = SCREEN_ID_MAIN,
        .name = "Main",

        .create = main_screen_create,
        .on_show = main_screen_on_show,
        .on_hide = main_screen_on_hide,
        .destroy = main_screen_destroy,
    };

    static const screen_desc_t settings_descriptor = {
        .id = SCREEN_ID_SETTINGS,
        .name = "Settings",

        .create = settings_screen_create,
        .on_show = settings_screen_on_show,
        .on_hide = settings_screen_on_hide,
        .destroy = settings_screen_destroy,
    };

    ESP_RETURN_ON_ERROR(
        screen_manager_register(
            &splash_descriptor
        ),
        TAG,
        "Failed to register splash screen"
    );

    ESP_RETURN_ON_ERROR(
        screen_manager_register(
            &main_descriptor
        ),
        TAG,
        "Failed to register main screen"
    );

    ESP_RETURN_ON_ERROR(
        screen_manager_register(
            &settings_descriptor
        ),
        TAG,
        "Failed to register settings screen"
    );

    return ESP_OK;
}

static esp_err_t gui_initialize_screens(void)
{
    ESP_RETURN_ON_ERROR(
        screen_manager_init(),
        TAG,
        "Failed to initialize screen manager"
    );

    ESP_RETURN_ON_ERROR(
        gui_register_screens(),
        TAG,
        "Failed to register screens"
    );

    ESP_RETURN_ON_ERROR(
        screen_manager_show(
            SCREEN_ID_SPLASH,
            LV_SCR_LOAD_ANIM_NONE,
            0
        ),
        TAG,
        "Failed to show splash screen"
    );

    return ESP_OK;
}

static void gui_task(void *argument)
{
    (void)argument;

    /*
     * Initialize display, input and LVGL here.
     */

    ESP_ERROR_CHECK(
        gui_initialize_screens()
    );

    while (true) {
        uint32_t delay_ms =
            lv_timer_handler();

        if (delay_ms < 5) {
            delay_ms = 5;
        }

        if (delay_ms > 20) {
            delay_ms = 20;
        }

        vTaskDelay(
            pdMS_TO_TICKS(delay_ms)
        );
    }
}

esp_err_t gui_service_start(void)
{
    BaseType_t result =
        xTaskCreatePinnedToCore(
            gui_task,
            "gui_task",
            GUI_TASK_STACK_SIZE,
            NULL,
            GUI_TASK_PRIORITY,
            NULL,
            1
        );

    if (result != pdPASS) {
        ESP_LOGE(
            TAG,
            "Failed to create GUI task"
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GUI service started");

    return ESP_OK;
}
