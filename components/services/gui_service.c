#include "gui_service.h"

#include <string.h>
#include <stdatomic.h>

#include "screens/splash_screen.h"
#include "screens/main_screen.h"
#include "screens/settings_screen.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "lvgl_port.h"
#include "gui_theme.h"
#include "gui_styles.h"
#include "screen_manager.h"
#include "settings_model.h"
#include "settings_service.h"

#define GUI_TASK_STACK_SIZE       (8192U)
#define GUI_TASK_PRIORITY         (5U)
#define GUI_BOOT_STATUS_LENGTH    (48U)
#define GUI_BOOT_QUEUE_LENGTH     (1U)

typedef struct
{
    uint8_t progress;
    char status[GUI_BOOT_STATUS_LENGTH];

} gui_boot_message_t;

static const char *TAG = "gui_service";

static QueueHandle_t s_boot_queue = NULL;

static bool s_initialized = false;
static atomic_bool s_started =
    ATOMIC_VAR_INIT(false);

/*
 * TODO:
 * Add complete LVGL and screen-manager cleanup together with startup
 * result synchronization before supporting GUI service restart.
 */

static void gui_service_process_boot_progress(void);

esp_err_t gui_service_set_boot_progress(
    uint8_t progress,
    const char *status
)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized ||
        s_boot_queue == NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    gui_boot_message_t message = {
        .progress =
            (progress <= 100U)
                ? progress
                : 100U,
        .status = {0},
    };

    strlcpy(
        message.status,
        status,
        sizeof(message.status)
    );

    if (xQueueOverwrite(
            s_boot_queue,
            &message
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    return ESP_OK;
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
            0U
        ),
        TAG,
        "Failed to show splash screen"
    );

    return ESP_OK;
}

esp_err_t gui_service_init(void)
{
    if (s_initialized ||
        s_boot_queue != NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    s_boot_queue = xQueueCreate(
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

    s_initialized = true;

    return ESP_OK;
}

static void gui_task(
    void *argument
)
{
    (void)argument;

    /*
     * Settings must be loaded before LVGL initialization because
     * shared styles capture the selected theme colors.
     */
    app_settings_t settings = {0};

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get GUI settings: %s",
            esp_err_to_name(result)
        );

        atomic_store(
            &s_started,
            false
        );

        vTaskDelete(NULL);
        return;
    }

    const gui_theme_mode_t theme_mode =
        settings.ui.theme_mode ==
            UI_THEME_MODE_DARK
                ? GUI_THEME_MODE_DARK
                : GUI_THEME_MODE_LIGHT;

    result =
        lvgl_port_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize LVGL port: %s",
            esp_err_to_name(result)
        );

        atomic_store(
            &s_started,
            false
        );

        vTaskDelete(NULL);
        return;
    }

    result =
        gui_theme_init(
            theme_mode
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize GUI theme: %s",
            esp_err_to_name(result)
        );

        atomic_store(
            &s_started,
            false
        );

        /*
         * LVGL is already initialized. Complete cleanup is required
         * before the GUI service can be started again safely.
         */
        vTaskDelete(NULL);
        return;
    }

    result =
        gui_styles_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize GUI styles: %s",
            esp_err_to_name(result)
        );

        atomic_store(
            &s_started,
            false
        );

        vTaskDelete(NULL);
        return;
    }

    result =
        settings_service_mark_theme_applied(
            settings.ui.theme_mode
        );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to record applied GUI theme: %s",
            esp_err_to_name(result)
        );
    }

    ESP_LOGI(
        TAG,
        "Shared GUI %s theme and styles initialized",
        theme_mode == GUI_THEME_MODE_DARK
            ? "dark"
            : "light"
    );

    result =
        gui_initialize_screens();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize GUI screens: %s",
            esp_err_to_name(result)
        );

        atomic_store(
            &s_started,
            false
        );

        vTaskDelete(NULL);
        return;
    }

    while (true) {
        gui_service_process_boot_progress();

        uint32_t delay_ms =
            lvgl_port_handler();

        if (delay_ms < 5U) {
            delay_ms = 5U;
        } else if (delay_ms > 20U) {
            delay_ms = 20U;
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                delay_ms
            )
        );
    }
}

esp_err_t gui_service_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_started,
            &expected,
            true
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t task_result =
        xTaskCreatePinnedToCore(
            gui_task,
            "gui_task",
            GUI_TASK_STACK_SIZE,
            NULL,
            GUI_TASK_PRIORITY,
            NULL,
            1
        );

    if (task_result != pdPASS) {
        atomic_store(
            &s_started,
            false
        );

        ESP_LOGE(
            TAG,
            "Failed to create GUI task"
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "GUI task created"
    );

    return ESP_OK;
}

static void gui_service_process_boot_progress(void)
{
    if (s_boot_queue == NULL) {
        return;
    }

    gui_boot_message_t message;

    if (xQueueReceive(
            s_boot_queue,
            &message,
            0U
        ) != pdTRUE) {

        return;
    }

    splash_screen_set_progress(
        message.progress,
        message.status
    );
}
