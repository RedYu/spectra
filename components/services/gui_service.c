#include "gui_service.h"

#include "display_driver.h"
#include "lvgl_port.h"

#include "screens/main_screen.h"
#include "screens/splash_screen.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"

static const char *TAG = "gui_service";

#define GUI_TASK_STACK_SIZE 8192
#define GUI_TASK_PRIORITY   5

static lv_obj_t *s_splash_screen;

static void gui_service_show_main_screen(void)
{
    lv_obj_t *main_screen =
        main_screen_create();

    if (main_screen == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create main screen"
        );

        return;
    }

    lv_screen_load_anim(
        main_screen,
        LV_SCR_LOAD_ANIM_FADE_IN,
        300,
        0,
        true
    );

    s_splash_screen = NULL;
}

void gui_service_set_loading_progress(
    uint8_t progress,
    const char *status
)
{
    splash_screen_set_progress(
        progress,
        status
    );
}

static void splash_test_timer_cb(
    lv_timer_t *timer
)
{
    static uint8_t progress;

    progress += 10;

    gui_service_set_loading_progress(
        progress,
        "Loading..."
    );

    if (progress >= 100) {
        progress = 0;

        lv_timer_delete(timer);

        gui_service_show_main_screen();
    }
}

static void gui_task(void *argument)
{
    (void)argument;

    s_splash_screen =
        splash_screen_create();

    if (s_splash_screen == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create splash screen"
        );

        vTaskDelete(NULL);
        return;
    }

    lv_screen_load(s_splash_screen);

    lv_timer_create(
        splash_test_timer_cb,
        200,
        NULL
    );

    display_driver_set_backlight(true);

    while (true) {
        uint32_t delay_ms =
            lvgl_port_handler();

        if (delay_ms == 0) {
            delay_ms = 1;
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
