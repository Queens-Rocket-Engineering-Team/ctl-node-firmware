idf_component_register(
    SRCS "ads112c04.c"
    INCLUDE_DIRS .
    REQUIRES
    esp_driver_gpio
    esp_driver_i2c
    freertos
)