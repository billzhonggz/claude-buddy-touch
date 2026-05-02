idf_component_register(
    SRCS "main.c" "display.c" "ble_nus.c" "touch.c" "state_machine.c"
    INCLUDE_DIRS "."
    REQUIRES esp32_p4_wifi6_touch_lcd_4_3 nvs_flash bt
)
