#include <esp_check.h>
#include <esp_err.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "sensor_base.h"
#include "thermistor.h"

static const char *TAG = "THERMISTOR";

static esp_err_t read_sensor(sensor_base_t *base, float *value) {
    if (base == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // cast base to thermistor_t, since it is the first member of struct
    return get_thermistor_reading((thermistor_t *)base, value);
}

esp_err_t thermistor_init(thermistor_t *thermistor, const thermistor_config_t *thermistor_cfg) {
    if (thermistor == NULL || thermistor_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_base_config_t base_cfg = {
        .adc = thermistor_cfg->adc,
        .p_pin = thermistor_cfg->pin,
        .n_pin = ADS112C04_AVSS,
        .gain = 1,
        .pga_enabled = false,
    };

    ESP_RETURN_ON_ERROR(
        sensor_base_init(&thermistor->base, &base_cfg), TAG, "Failed to initialize thermistor sensor"
    );

    thermistor->base.read_sensor = read_sensor;
    thermistor->beta = thermistor_cfg->beta;
    thermistor->resistor_ohms = thermistor_cfg->resistor_ohms;
    thermistor->base.unit = thermistor_cfg->unit;
    return ESP_OK;
}

esp_err_t get_thermistor_reading(thermistor_t *thermistor, float *temperature) {
    if (thermistor == NULL || temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float voltage = 0;
    ESP_RETURN_ON_ERROR(
        sensor_base_voltage_reading(&thermistor->base, &voltage), TAG, "Failed to get thermistor voltage reading"
    );
    if (voltage >= 5.0f || voltage <= 0.0f) {
        ESP_LOGW(TAG, "Voltage reading out of bounds");
        return ESP_FAIL;
    }

    const float resistance = voltage * thermistor->resistor_ohms / (5.0f - voltage);

    const float temp_K = 1.0f / ((1.0f / 298.15f) + (1.0f / thermistor->beta) * log(resistance / thermistor->resistor_ohms));

    if (thermistor->base.unit == SENSOR_UNIT_C) {
        *temperature = temp_K - 273.15f;
    } else if (thermistor->base.unit == SENSOR_UNIT_K) {
        *temperature = temp_K;
    } else if (thermistor->base.unit == SENSOR_UNIT_F) {
        *temperature = ((temp_K - 273.15f) * 1.8) + 32;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
