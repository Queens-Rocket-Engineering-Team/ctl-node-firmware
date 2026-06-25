#pragma once

#include <driver/i2c_master.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <stdatomic.h>

#include "ads112c04.h"
#include "config_json.h"
#include "wifi_tools.h"

typedef struct {
    ads112c04_t adcs[CONFIG_NUM_ADCS];
    config_sensor_t sensors[CONFIG_NUM_SENSORS];
    control_t controls[CONFIG_NUM_CONTROLS];
    network_ctx_t *network_ctx;
    EventGroupHandle_t sensor_stream_event_group_handle;
    TaskHandle_t sensor_stream_handle;
    atomic_uint_least32_t ts_offset;
    atomic_uint_least16_t sequence;
    i2c_master_bus_handle_t bus_handle;
} app_ctx_t;

esp_err_t app_setup(app_ctx_t *app_ctx);