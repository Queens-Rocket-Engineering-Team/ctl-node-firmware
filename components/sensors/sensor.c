#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "sensor.h"

static const char *TAG = "SENSOR";

esp_err_t sensor_init(sensor_t *sensor, const sensor_config_t *sensor_cfg) {
    if (sensor == NULL || sensor_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor_cfg->adc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ads112c04_is_mux_valid(sensor_cfg->p_pin, sensor_cfg->n_pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ads112c04_is_gain_valid(sensor_cfg->gain)) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor->adc = sensor_cfg->adc;
    sensor->p_pin = sensor_cfg->p_pin;
    sensor->n_pin = sensor_cfg->n_pin;
    sensor->gain = sensor_cfg->gain;
    sensor->pga_enabled = sensor_cfg->pga_enabled;

    return ESP_OK;
}

esp_err_t sensor_voltage_reading(sensor_t *sensor, float *voltage) {
    ESP_RETURN_ON_ERROR(
        ads112c04_get_single_voltage_reading(
            sensor->adc, voltage, sensor->p_pin, sensor->n_pin, sensor->gain, sensor->pga_enabled
        ),
        TAG,
        "Failed to get sensor reading"
    );
    return ESP_OK;
}