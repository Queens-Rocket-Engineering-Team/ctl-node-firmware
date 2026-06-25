#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "current_sensor.h"
#include "sensor.h"

static const char *TAG = "CURRENT SENSOR";

esp_err_t current_sensor_init(current_sensor_t *current_sensor, const current_sensor_config_t *current_sensor_cfg) {
    if (current_sensor == NULL || current_sensor_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_config_t sensor_cfg = {
        .adc = current_sensor_cfg->adc,
        .p_pin = current_sensor_cfg->pin,
        .n_pin = ADS112C04_AVSS,
        .gain = 1,
        .pga_enabled = false,
    };

    ESP_RETURN_ON_ERROR(sensor_init(&current_sensor->sensor, &sensor_cfg), TAG, "Failed to initialize current sensor");

    current_sensor->shunt_resistor_ohms = current_sensor_cfg->shunt_resistor_ohms;
    current_sensor->csa_gain = current_sensor_cfg->csa_gain;
    current_sensor->unit = current_sensor_cfg->unit;
    return ESP_OK;
}

esp_err_t get_current_reading(current_sensor_t *current_sensor, float *current) {
    if (current_sensor == NULL || current == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float voltage = 0;
    ESP_RETURN_ON_ERROR(
        sensor_voltage_reading(&current_sensor->sensor, &voltage), TAG, "Failed to get current sensor voltage reading"
    );

    const float current_A = (voltage / current_sensor->shunt_resistor_ohms) / current_sensor->csa_gain;
    if (current_sensor->unit == CURRENT_SENSOR_A) {
        *current = current_A;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
