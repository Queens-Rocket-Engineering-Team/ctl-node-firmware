#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "hall_effect_current.h"
#include "sensor_base.h"

static const char *TAG = "HALL EFFECT CURRENT SENSOR";

static esp_err_t read_sensor(sensor_base_t *base, float *value) {
    if (base == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // cast base to current_sensor_t, since it is the first member of struct
    return get_he_current_reading((he_current_t *)base, value);
}

esp_err_t he_current_init(he_current_t *current_sensor, const he_current_config_t *current_sensor_cfg) {
    if (current_sensor == NULL || current_sensor_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_base_config_t base_cfg = {
        .adc = current_sensor_cfg->adc,
        .p_pin = current_sensor_cfg->pin,
        .n_pin = ADS112C04_AVSS,
        .gain = 1,
        .pga_enabled = false,
    };

    ESP_RETURN_ON_ERROR(sensor_base_init(&current_sensor->base, &base_cfg), TAG, "Failed to initialize current sensor");

    current_sensor->base.read_sensor = read_sensor;
    current_sensor->sensitivity_mV_A = current_sensor_cfg->sensitivity_mV_A;
    current_sensor->base.unit = current_sensor_cfg->unit;
    return ESP_OK;
}

esp_err_t get_he_current_reading(he_current_t *current_sensor, float *current) {
    if (current_sensor == NULL || current == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float voltage = 0;
    ESP_RETURN_ON_ERROR(
        sensor_base_voltage_reading(&current_sensor->base, &voltage),
        TAG,
        "Failed to get current sensor voltage reading"
    );

    // sensor outputs 0.5V at 0A (assumes VCC is 5V right now, 0.1*VCC)
    const float current_A = (voltage - 0.5f) * (1000.0f / current_sensor->sensitivity_mV_A);
    if (current_sensor->base.unit == SENSOR_UNIT_A) {
        *current = current_A;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
