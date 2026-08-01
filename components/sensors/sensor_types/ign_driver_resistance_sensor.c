#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "ign_driver_resistance_sensor.h"
#include "sensor_base.h"

static const char *TAG = "IGN DRIVER RESISTANCE_SENSOR";

static esp_err_t read_sensor(sensor_base_t *base, float *value) {
    if (base == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // cast base to ign_driver_res_sensor_t, since it is the first member of struct
    return get_ign_driver_res_reading((ign_driver_res_sensor_t *)base, value);
}

esp_err_t ign_driver_res_sensor_init(
    ign_driver_res_sensor_t *resistance_sensor,
    const ign_driver_res_sensor_config_t *resistance_sensor_cfg
) {
    if (resistance_sensor == NULL || resistance_sensor_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_base_config_t base_cfg = {
        .adc = resistance_sensor_cfg->adc,
        .p_pin = resistance_sensor_cfg->p_pin,
        .n_pin = resistance_sensor_cfg->n_pin,
        .gain = 1,
        .pga_enabled = false,
    };

    ESP_RETURN_ON_ERROR(
        sensor_base_init(&resistance_sensor->base, &base_cfg), TAG, "Failed to initialize ignitor resistance sensor"
    );

    resistance_sensor->base.read_sensor = read_sensor;
    resistance_sensor->injected_current_A = resistance_sensor_cfg->injected_current_mA / 1000.0f;
    resistance_sensor->preprocessing_gain = resistance_sensor_cfg->preprocessing_gain;
    resistance_sensor->base.unit = resistance_sensor_cfg->unit;
    
    return ESP_OK;
}

esp_err_t get_ign_driver_res_reading(ign_driver_res_sensor_t *resistance_sensor, float *resistance) {
    if (resistance_sensor == NULL || resistance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    float voltage = 0;
    ESP_RETURN_ON_ERROR(
        sensor_base_voltage_reading(&resistance_sensor->base, &voltage), TAG, "Failed to get ignitor resistance voltage reading"
    );

    const float resistance_ohms = (voltage / resistance_sensor->preprocessing_gain) / (resistance_sensor->injected_current_A);
    if (resistance_sensor->base.unit == SENSOR_UNIT_OHMS) {
        *resistance = resistance_ohms;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}