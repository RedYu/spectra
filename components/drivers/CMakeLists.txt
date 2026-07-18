idf_component_register(
    SRCS
        "display_driver.c"
        "touch_driver.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        board
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911

    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_driver_i2c
        esp_timer
        esp_lcd_ili9488
)
