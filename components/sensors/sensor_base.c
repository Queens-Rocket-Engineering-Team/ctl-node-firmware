#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "sensor_base.h"

static const char *TAG = "SENSOR";

esp_err_t sensor_base_init(sensor_base_t *sensor_base, const sensor_base_config_t *sensor_base_cfg) {
    if (sensor_base == NULL || sensor_base_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor_base_cfg->adc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ads112c04_is_mux_valid(sensor_base_cfg->p_pin, sensor_base_cfg->n_pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ads112c04_is_gain_valid(sensor_base_cfg->gain)) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor_base->adc = sensor_base_cfg->adc;
    sensor_base->p_pin = sensor_base_cfg->p_pin;
    sensor_base->n_pin = sensor_base_cfg->n_pin;
    sensor_base->gain = sensor_base_cfg->gain;
    sensor_base->pga_enabled = sensor_base_cfg->pga_enabled;

    return ESP_OK;
}

esp_err_t sensor_base_voltage_reading(sensor_base_t *sensor_base, float *voltage) {
    ESP_RETURN_ON_ERROR(
        ads112c04_get_single_voltage_reading(
            sensor_base->adc,
            voltage,
            sensor_base->p_pin,
            sensor_base->n_pin,
            sensor_base->gain,
            sensor_base->pga_enabled
        ),
        TAG,
        "Failed to get sensor reading"
    );
    return ESP_OK;
}