idf_component_register(
    SRCS "hardware_bool_control/h_bool_control.c" "virtual_uint32_control/virtual_uint32_control.c" "queue_registry.c"
    INCLUDE_DIRS . "hardware_bool_control/" "virtual_uint32_control"
    REQUIRES
    esp_common
    esp_driver_gpio
)