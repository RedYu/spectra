#include "gui_service.h"

#include "lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"

#include "assets/logo_cpg.h"
#include "display_driver.h"

#include "esp_random.h"

#include "gui/screens/settings_screen.h"

static const char *TAG = "gui_service";

#define GUI_TASK_STACK_SIZE 8192
#define GUI_TASK_PRIORITY   5

static lv_obj_t *s_splash_screen;
static lv_obj_t *s_main_screen;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_status_label;

static void gui_create_splash_screen(void)
{
    s_splash_screen = lv_obj_create(NULL);

    lv_obj_remove_flag(s_splash_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(
        s_splash_screen,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_splash_screen,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_t *logo_image = lv_image_create(s_splash_screen);

    lv_image_set_src(logo_image, &logo_cpg);

    lv_obj_align(
        logo_image,
        LV_ALIGN_CENTER,
        0,
        -45
    );

    lv_obj_t *title_label = lv_label_create(s_splash_screen);

    lv_label_set_text(title_label, "SPECTRA");

    lv_obj_set_style_text_font(
        title_label,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        title_label,
        lv_color_hex(0x808080),
        LV_PART_MAIN
    );

    lv_obj_align(
        title_label,
        LV_ALIGN_CENTER,
        0,
        55
    );

    s_status_label = lv_label_create(s_splash_screen);

    lv_label_set_text(s_status_label, "Starting system...");

    lv_obj_set_style_text_font(
        s_status_label,
        &lv_font_montserrat_16,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_status_label,
        lv_color_hex(0xA0A8B0),
        LV_PART_MAIN
    );

    lv_obj_align(
        s_status_label,
        LV_ALIGN_BOTTOM_MID,
        0,
        -48
    );

    s_progress_bar = lv_bar_create(s_splash_screen);

    lv_obj_set_size(
        s_progress_bar,
        280,
        8
    );

    lv_obj_align(
        s_progress_bar,
        LV_ALIGN_BOTTOM_MID,
        0,
        -25
    );

    lv_bar_set_range(
        s_progress_bar,
        0,
        100
    );

    lv_bar_set_value(
        s_progress_bar,
        0,
        LV_ANIM_OFF
    );

    lv_screen_load(s_splash_screen);
}

static void settings_button_event_cb(lv_event_t *event)
{
    /*
     * Open the settings screen.
     */
    settings_screen_create();
}

static lv_obj_t *gui_create_toolbar(lv_obj_t *parent)
{
    /*
     * Create the top toolbar.
     */
    lv_obj_t *toolbar = lv_obj_create(parent);

    lv_obj_set_size(
        toolbar,
        LV_PCT(100),
        56
    );

    lv_obj_align(
        toolbar,
        LV_ALIGN_TOP_MID,
        0,
        0
    );

    lv_obj_remove_flag(
        toolbar,
        LV_OBJ_FLAG_SCROLLABLE
    );

    /*
     * Remove the default container border and radius.
     */
    lv_obj_set_style_border_width(
        toolbar,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        toolbar,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_left(
        toolbar,
        16,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_right(
        toolbar,
        8,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_top(
        toolbar,
        4,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_bottom(
        toolbar,
        4,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        toolbar,
        lv_color_hex(0x20252A),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        toolbar,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /*
     * Arrange toolbar elements horizontally.
     */
    lv_obj_set_flex_flow(
        toolbar,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        toolbar,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    /*
     * Create the application title.
     */
    lv_obj_t *title_label = lv_label_create(toolbar);

    lv_label_set_text(
        title_label,
        "SPECTRA"
    );

    lv_obj_set_style_text_font(
        title_label,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        title_label,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    /*
     * Create the settings button.
     */
    lv_obj_t *settings_button = lv_button_create(toolbar);

    lv_obj_set_size(
        settings_button,
        44,
        44
    );

    lv_obj_set_style_radius(
        settings_button,
        10,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        settings_button,
        lv_color_hex(0x343B42),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        settings_button,
        lv_color_hex(0x4B77D1),
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_set_style_shadow_width(
        settings_button,
        0,
        LV_PART_MAIN
    );

    lv_obj_add_event_cb(
        settings_button,
        settings_button_event_cb,
        LV_EVENT_CLICKED,
        NULL
    );

    /*
     * Create the settings icon.
     */
    lv_obj_t *settings_icon = lv_label_create(settings_button);

    lv_label_set_text(
        settings_icon,
        LV_SYMBOL_SETTINGS
    );

    lv_obj_set_style_text_color(
        settings_icon,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_font(
        settings_icon,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_center(settings_icon);

    return toolbar;
}

static void gui_create_main_screen(void)
{
    s_main_screen = lv_obj_create(NULL);

    lv_obj_remove_flag(
        s_main_screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_bg_color(
        s_main_screen,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_main_screen,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /*
     * Create the top toolbar.
     */
    gui_create_toolbar(s_main_screen);

    /*
     * Create the main content container.
     */
    lv_obj_t *content = lv_obj_create(s_main_screen);

    lv_obj_set_size(
        content,
        LV_PCT(100),
        320 - 56
    );

    lv_obj_align(
        content,
        LV_ALIGN_BOTTOM_MID,
        0,
        0
    );

    lv_obj_remove_flag(
        content,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        content,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        content,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        content,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        content,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /*
     * Create the status label.
     */
    lv_obj_t *label = lv_label_create(content);

    lv_label_set_text(
        label,
        "System ready"
    );

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        label,
        lv_color_hex(0x20252A),
        LV_PART_MAIN
    );

    lv_obj_center(label);
}

void gui_service_set_loading_progress(
    uint8_t progress,
    const char *status
)
{
    if (progress > 100) {
        progress = 100;
    }

    lv_bar_set_value(
        s_progress_bar,
        progress,
        LV_ANIM_ON
    );

    if (status != NULL) {
        lv_label_set_text(
            s_status_label,
            status
        );
    }
}

void gui_service_show_main_screen(void)
{
    gui_create_main_screen();

    lv_screen_load_anim(
        s_main_screen,
        LV_SCR_LOAD_ANIM_FADE_IN,
        300,
        0,
        true
    );

    s_splash_screen = NULL;
    s_progress_bar = NULL;
    s_status_label = NULL;
}

static void splash_test_timer_cb(lv_timer_t *timer)
{
    static uint8_t progress = 0;

    progress += 10;

    gui_service_set_loading_progress(
        progress,
        "Loading..."
    );

    if (progress >= 100) {
        lv_timer_delete(timer);
        gui_service_show_main_screen();
    }
}


static void gui_task(void *arg)
{
    (void)arg;

    gui_create_splash_screen();

    lv_timer_create(
        splash_test_timer_cb,
        200,
        NULL
    );

    display_driver_set_backlight(true);

    while (true) {
        uint32_t delay_ms = lvgl_port_handler();

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}


esp_err_t gui_service_start(void)
{
    BaseType_t result = xTaskCreatePinnedToCore(
        gui_task,
        "gui_task",
        GUI_TASK_STACK_SIZE,
        NULL,
        GUI_TASK_PRIORITY,
        NULL,
        1
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GUI task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
