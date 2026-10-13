idf_component_register(
    SRCS "main.c" "board.c" "battery.c" "display.c" "ui.c" "font.c" "font_montserrat.c" "usb_link.c" "ble_link.c" "touch.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_lcd esp_timer efuse bt nvs_flash
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
