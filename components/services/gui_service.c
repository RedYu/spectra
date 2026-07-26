#include "gui_service.h"

#include <string.h>

#include "screens/splash_screen.h"
#include "screens/main_screen.h"
#include "screens/settings_screen.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "screen_manager.h"
#include "gui_config.h"

static const char *TAG = "gui_service";

#define GUI_TASK_STACK_SIZE 8192
#define GUI_TASK_PRIORITY   5

#define GUI_BOOT_STATUS_LENGTH 48
#define GUI_BOOT_QUEUE_LENGTH  8

typedef struct
{
    uint8_t progress;
    char status[GUI_BOOT_STATUS_LENGTH];

} gui_boot_message_t;

static QueueHandle_t s_boot_queue = NULL;
static bool s_main_screen_opened = false;

static void gui_service_process_boot_progress(void)
{
    if (s_boot_queue == NULL) {
        return;
    }

    gui_boot_message_t message;

    while (xQueueReceive(
            s_boot_queue,
            &message,
            0
        ) == pdTRUE) {

        splash_screen_set_progress(
            message.progress,
            message.status
        );

        if (message.progress >= 100 &&
            !s_main_screen_opened) {

            s_main_screen_opened = true;

            screen_manager_show(
                SCREEN_ID_MAIN,
                gui_config_are_animations_enabled() ? LV_SCR_LOAD_ANIM_FADE_IN : LV_SCR_LOAD_ANIM_NONE,
                gui_config_are_animations_enabled() ? 300 : 0
            );
        }
    }
}

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

esp_err_t gui_service_init(void) 
{
    s_boot_queue =
        xQueueCreate(
            GUI_BOOT_QUEUE_LENGTH,
            sizeof(gui_boot_message_t)
        );

    if (s_boot_queue == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create boot progress queue"
        );

        return ESP_ERR_NO_MEM;
    }

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
        gui_service_process_boot_progress();

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

void gui_service_set_boot_progress(
    uint8_t progress,
    const char *status
)
{
    if (s_boot_queue == NULL) {
        return;
    }

    gui_boot_message_t message = {
        .progress = progress,
    };

    strlcpy(
        message.status,
        status != NULL ? status : "",
        sizeof(message.status)
    );

    if (xQueueSend(
            s_boot_queue,
            &message,
            0
        ) != pdTRUE) {

        /*
         * If the queue is full, remove the oldest message
         * and preserve the newest progress value.
         */
        gui_boot_message_t discarded;

        xQueueReceive(
            s_boot_queue,
            &discarded,
            0
        );

        xQueueSend(
            s_boot_queue,
            &message,
            0
        );
    }
}
