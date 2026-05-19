#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "pressure_transducer.h"
#include "sensor_base.h"

static const char *TAG = "PRESSURE TRANSDUCER";

static esp_err_t read_sensor(sensor_base_t *base, float *value) {
    if (base == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // cast base to pressure_transducer_t, since it is the first member of struct
    return get_pressure_transducer_reading((pressure_transducer_t *)base, value);
}

esp_err_t pressure_transducer_init(
    pressure_transducer_t *pressure_transducer,
    const pressure_transducer_config_t *pressure_transducer_cfg
) {
    if (pressure_transducer == NULL || pressure_transducer_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pressure_transducer_cfg->resistor_ohms < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_base_config_t base_cfg = {
        .adc = pressure_transducer_cfg->adc,
        .p_pin = pressure_transducer_cfg->pin,
        .n_pin = ADS112C04_AVSS,
        .gain = 1,
        .pga_enabled = false,
    };

    ESP_RETURN_ON_ERROR(
        sensor_init(&pressure_transducer->base, &base_cfg), TAG, "Failed to initialize pressure transducer sensor"
    );

    pressure_transducer->base.read_sensor = get_pressure_reading;
    pressure_transducer->resistor_ohms = pressure_transducer_cfg->resistor_ohms;
    pressure_transducer->max_pressure_psi = pressure_transducer_cfg->max_pressure_psi;
    pressure_transducer->unit = pressure_transducer_cfg->unit;
    return ESP_OK;
}

esp_err_t get_pressure_reading(pressure_transducer_t *pressure_transducer, float *pressure) {
    if (pressure_transducer == NULL || pressure == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    float voltage = 0;
    ESP_RETURN_ON_ERROR(
        sensor_base_voltage_reading(&pressure_transducer->base, &voltage),
        TAG,
        "Failed to get pressure transducer voltage reading"
    );

    float current_mA = 1000 * voltage / pressure_transducer->resistor_ohms;

    // 4-20 mA pressure transducer
    const float psi = ((current_mA - 4) / 16) * pressure_transducer->max_pressure_psi;
    if (pressure_transducer->unit == PRESSURE_TRANSDUCER_PSI) {
        *pressure = psi;
    } else if (pressure_transducer->unit == PRESSURE_TRANSDUCER_BAR) {
        *pressure = psi * 0.0689476;
    } else if (pressure_transducer->unit == PRESSURE_TRANSDUCER_PA) {
        *pressure = psi * 6894.76;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
