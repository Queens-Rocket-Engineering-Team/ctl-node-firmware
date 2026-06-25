#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include "ads112c04.h"
#include "config_json.h"
#include "sensor_stream.h"
#include "setup.h"
#include "wifi_tools.h"

static const char *TAG = "SETUP";

static esp_err_t s_network_setup(network_ctx_t *network_ctx) {
    if (network_ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // initialize NVS for wifi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase NVS");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to initialize NVS");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize NETIF");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create default event loop");
    network_ctx->netif_handle = esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_config), TAG, "Failed to initialize wifi driver");

    ESP_RETURN_ON_ERROR(wifi_event_handler_register(network_ctx), TAG, "Failed to register wifi event handler");

    wifi_config_t wifi_config = {
        .sta = {
                .ssid = CONFIG_ESP_WIFI_SSID,
                .password = CONFIG_ESP_WIFI_PASSWORD,
                },
    };

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Failed to set wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start wifi driver");

    ESP_RETURN_ON_ERROR(network_manager_init(network_ctx), TAG, "Failed to initialize network manager");

    return ESP_OK;
}

esp_err_t app_setup(app_ctx_t *app_ctx) {
    if (app_ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // initialize I2C bus
    i2c_master_bus_config_t i2c_mst_config = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_SCL_PIN,
        .sda_io_num = CONFIG_SDA_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false, // external 1k pullups, dont need
    };

    // initialize i2c master
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_mst_config, &app_ctx->bus_handle), TAG, "Failed to create I2C bus");

    // install isr service for adcs
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(ESP_INTR_FLAG_IRAM), TAG, "Failed to install ISR service");

    // set up adcs on i2c bus
    ESP_RETURN_ON_ERROR(
        config_ads112c04s_init(app_ctx->adcs, CONFIG_NUM_ADCS, app_ctx->bus_handle), TAG, "Failed to initialize ADCs"
    );

    for (size_t i = 0; i < CONFIG_NUM_ADCS; i++) {
        uint8_t adc_addr = ads112c04_get_address(&app_ctx->adcs[i]);
        ESP_RETURN_ON_ERROR(i2c_master_probe(app_ctx->bus_handle, adc_addr, 50), TAG, "ADC not found on I2C bus");
    }

    ESP_RETURN_ON_ERROR(
        config_sensors_init(app_ctx->sensors, CONFIG_NUM_SENSORS, app_ctx->adcs, CONFIG_NUM_ADCS),
        TAG,
        "Failed to initialize sensors"
    );

    ESP_RETURN_ON_ERROR(
        config_controls_init(app_ctx->controls, CONFIG_NUM_CONTROLS), TAG, "Failed to initialize controls"
    );

    // set up sensor stream event group
    static StaticEventGroup_t xEventGroup_SENSORSTREAM;

    app_ctx->sensor_stream_event_group_handle = xEventGroupCreateStatic(&xEventGroup_SENSORSTREAM);
    configASSERT(app_ctx->sensor_stream_event_group_handle);

    // set up sensor stream task
    static StaticTask_t xTaskBuffer_SENSORSTREAM;
    static StackType_t xStack_SENSORSTREAM[SENSOR_STREAM_STACK_SIZE];

    app_ctx->sensor_stream_handle = xTaskCreateStatic(
        sensor_stream,
        "Sensor Stream",
        SENSOR_STREAM_STACK_SIZE,
        (void *)app_ctx,
        1,
        xStack_SENSORSTREAM,
        &xTaskBuffer_SENSORSTREAM
    );
    configASSERT(app_ctx->sensor_stream_handle);

    app_ctx->sequence = 0;
    app_ctx->ts_offset = 0;

    // set up network manager
    static network_ctx_t network_ctx = {0};
    ESP_RETURN_ON_ERROR(s_network_setup(&network_ctx), TAG, "Failed to set up network_ctx");
    app_ctx->network_ctx = &network_ctx;

    return ESP_OK;
}
