#include "settings_screen.h"

#include "lvgl.h"

static lv_obj_t *s_settings_screen;
static lv_obj_t *s_menu;

static void back_to_main_event_cb(lv_event_t *event);
static void brightness_changed_event_cb(lv_event_t *event);

static lv_obj_t *create_menu_row(
    lv_obj_t *parent,
    const char *title
)
{
    lv_obj_t *container = lv_menu_cont_create(parent);

    lv_obj_set_flex_flow(
        container,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t *label = lv_label_create(container);

    lv_label_set_text(label, title);

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_18,
        LV_PART_MAIN
    );

    lv_obj_set_flex_grow(label, 1);

    return container;
}

static void create_brightness_row(lv_obj_t *parent)
{
    lv_obj_t *container = create_menu_row(
        parent,
        "Brightness"
    );

    lv_obj_t *slider = lv_slider_create(container);

    lv_obj_set_width(slider, 150);

    lv_slider_set_range(
        slider,
        10,
        100
    );

    lv_slider_set_value(
        slider,
        80,
        LV_ANIM_OFF
    );

    lv_obj_add_event_cb(
        slider,
        brightness_changed_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );
}

static void create_switch_row(
    lv_obj_t *parent,
    const char *title,
    bool enabled
)
{
    lv_obj_t *container = create_menu_row(
        parent,
        title
    );

    lv_obj_t *switch_obj = lv_switch_create(container);

    if (enabled) {
        lv_obj_add_state(
            switch_obj,
            LV_STATE_CHECKED
        );
    }
}

static lv_obj_t *create_display_page(void)
{
    lv_obj_t *page = lv_menu_page_create(
        s_menu,
        "Display"
    );

    lv_obj_t *section = lv_menu_section_create(page);

    create_brightness_row(section);
    create_switch_row(section, "Dark mode", true);
    create_switch_row(section, "Animations", true);

    return page;
}

static lv_obj_t *create_can_page(void)
{
    lv_obj_t *page = lv_menu_page_create(
        s_menu,
        "CAN"
    );

    lv_obj_t *section = lv_menu_section_create(page);

    lv_obj_t *bitrate_row = create_menu_row(
        section,
        "Bitrate"
    );

    lv_obj_t *dropdown = lv_dropdown_create(
        bitrate_row
    );

    lv_dropdown_set_options(
        dropdown,
        "125 kbit/s\n"
        "250 kbit/s\n"
        "500 kbit/s\n"
        "1 Mbit/s"
    );

    lv_dropdown_set_selected(dropdown, 2);

    create_switch_row(
        section,
        "Listen-only mode",
        false
    );

    create_switch_row(
        section,
        "CAN termination",
        true
    );

    return page;
}

static lv_obj_t *create_network_page(void)
{
    lv_obj_t *page = lv_menu_page_create(
        s_menu,
        "Network"
    );

    lv_obj_t *section = lv_menu_section_create(page);

    create_switch_row(
        section,
        "Wi-Fi",
        true
    );

    create_switch_row(
        section,
        "USB network",
        true
    );

    return page;
}

static void add_page_link(
    lv_obj_t *parent,
    const char *title,
    lv_obj_t *target_page
)
{
    lv_obj_t *container = create_menu_row(
        parent,
        title
    );

    lv_obj_t *arrow = lv_label_create(container);

    lv_label_set_text(
        arrow,
        LV_SYMBOL_RIGHT
    );

    lv_menu_set_load_page_event(
        s_menu,
        container,
        target_page
    );
}

static void create_root_page(void)
{
    lv_obj_t *root_page = lv_menu_page_create(
        s_menu,
        "Settings"
    );

    lv_obj_t *system_section = lv_menu_section_create(
        root_page
    );

    lv_obj_t *display_page = create_display_page();
    lv_obj_t *can_page = create_can_page();
    lv_obj_t *network_page = create_network_page();

    add_page_link(
        system_section,
        "Display",
        display_page
    );

    add_page_link(
        system_section,
        "CAN",
        can_page
    );

    add_page_link(
        system_section,
        "Network",
        network_page
    );

    lv_obj_t *general_section = lv_menu_section_create(
        root_page
    );

    create_switch_row(
        general_section,
        "Sound",
        false
    );

    create_switch_row(
        general_section,
        "Developer mode",
        false
    );

    lv_menu_set_page(
        s_menu,
        root_page
    );
}

static void brightness_changed_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);

    int32_t brightness = lv_slider_get_value(slider);

    /*
     * Apply the selected brightness level
     * to the display backlight here.
     */
    // display_driver_set_backlight_level(brightness);
}

void settings_screen_create(void)
{
    s_settings_screen = lv_obj_create(NULL);

    lv_obj_remove_flag(
        s_settings_screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_bg_color(
        s_settings_screen,
        lv_color_hex(0x101418),
        LV_PART_MAIN
    );

    s_menu = lv_menu_create(
        s_settings_screen
    );

    lv_obj_set_size(
        s_menu,
        LV_PCT(100),
        LV_PCT(100)
    );

    lv_obj_center(s_menu);

    lv_menu_set_mode_header(
        s_menu,
        LV_MENU_HEADER_TOP_FIXED
    );

    create_root_page();

    lv_screen_load(s_settings_screen);
}
