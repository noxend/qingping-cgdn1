# The panel, its text layer, the LED, the touch strip, the button, the battery. Drivers call nothing
# above them: the touch strip reports through the callbacks it is started with.
idf_component_register(SRCS "oled.c" "oled_compose.c" "text.c" "text_message.c" "led.c"
                            "touch.c" "touch_bus.c" "button.c" "button_press.c"
                            "battery.c"
                       INCLUDE_DIRS "."
                       REQUIRES u8g2
                       PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_i2c esp_driver_ledc
                                     esp_driver_spi esp_timer)
