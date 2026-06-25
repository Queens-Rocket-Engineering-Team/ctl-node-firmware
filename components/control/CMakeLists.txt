idf_component_register(
    SRCS "control.c"
    INCLUDE_DIRS .
    REQUIRES esp_common esp_driver_gpio
)