idf_component_register(
    SRCS
        "sd_card_driver.c"
        "display_backlight.c"
        "display_driver.c"
        "touch_driver.c"
        "axp313a_driver.c"
        "battery_adc_driver.c"
        "buzzer_driver.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        board
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
        fatfs
        sdmmc
        esp_driver_sdspi

    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_driver_i2c
        esp_driver_ledc
        esp_timer
        esp_lcd_ili9488
        esp_adc
        app
)
