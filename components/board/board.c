#include "board.h"
#include "display_backlight.h"

esp_err_t board_init(void)
{
    ESP_ERROR_CHECK(
        display_backlight_init()
    );

    ESP_ERROR_CHECK(
        display_backlight_set_brightness(
            80
        )
    );

    return ESP_OK;
}
