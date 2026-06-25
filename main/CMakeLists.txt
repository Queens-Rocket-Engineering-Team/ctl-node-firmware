idf_component_register(
    SRCS "main.c" "setup.c" "sensor_stream.c" "wifi/network_manager.c" "wifi/tcp_tools.c" "wifi/ssdp_tools.c" "wifi/udp_tools.c"
    INCLUDE_DIRS . ./wifi
    REQUIRES
    control
    ads112c04
    config_json
    qlcp_lib
    esp_common
    esp_driver_i2c
    esp_driver_gpio
    freertos
    esp_wifi
    esp_timer
    nvs_flash
    esp_event
)
