idf_component_register(
    SRCS "esp32_p4_wifi6_touch_lcd_4_3.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_sdmmc esp_driver_spi esp_hw_support sdmmc esp_timer esp_lcd
    PRIV_REQUIRES spiffs fatfs
)
