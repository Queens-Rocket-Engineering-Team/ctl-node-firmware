#include <esp_check.h>
#include <esp_err.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "sensor.h"
#include "thermocouple.h"

static const char *TAG = "PRESSURE TRANSDUCER";

// Temperature to voltage coefficients

static const float cP[] = {
    -1.76e-02,
    3.89e-02,
    1.86e-05,
    -9.95e-08,
    3.18e-10,
    -5.61e-13,
    5.61e-16,
    -3.20e-19,
    9.72e-23,
    -1.21e-26,
};

static const float a[] = {
    1.185976e-01,
    -1.183432e-04,
    1.269686e+02,
};

static const float cN[] = {
    0.00e+00,
    3.95e-02,
    2.36e-05,
    -3.29e-07,
    -4.99e-09,
    -6.75e-11,
    -5.74e-13,
    -3.11e-15,
    -1.05e-17,
    -1.99e-20,
    -1.63e-23,
};

// Voltage to temperature coefficients

static const float d1[] = {
    0.0000000E+00,
    2.5173462E+01,
    -1.1662878E+00,
    -1.0833638E+00,
    -8.9773540E-01,
    -3.7342377E-01,
    -8.6632643E-02,
    -1.0450598E-02,
    -5.1920577E-04
};

static const float d2[] = {
    0.000000E+00,
    2.508355E+01,
    7.860106E-02,
    -2.503131E-01,
    8.315270E-02,
    -1.228034E-02,
    9.804036E-04,
    -4.413030E-05,
    1.057734E-06,
    -1.052755E-08,
};

static const float d3[] = {
    -1.318058E+02,
    4.830222E+01,
    -1.646031E+00,
    5.464731E-02,
    -9.650715E-04,
    8.802193E-06,
    -3.110810E-08,
};

static float s_poly_eval(float x, const float coeffs[], size_t coeffs_len) {
    // horner's method
    float y = 0;
    for (ssize_t i = coeffs_len - 1; i >= 0; i--) {
        y = y * x + coeffs[i];
    }
    return y;
}

static float s_temperature_to_voltage(float temperature) {
    // Equations from the NIST Temperature Scale Database:
    // https://its90.nist.gov/RefFunctions
    float voltage_mV = 0;
    if (temperature > 0) {
        voltage_mV = s_poly_eval(temperature, cP, sizeof(cP) / sizeof(cP[0]));
        voltage_mV += a[0] * exp(a[1] * (temperature - a[2] * a[2]));
    } else {
        voltage_mV = s_poly_eval(temperature, cN, sizeof(cN) / sizeof(cN[0]));
    }
    return voltage_mV;
}

static float s_voltage_to_temperature(float voltage_mV) {
    // Equations from the NIST Temperature Scale Database:
    // https://its90.nist.gov/InvFunctions
    float temperature = 0;
    if (voltage_mV < 0.0f) {
        temperature = s_poly_eval(voltage_mV, d1, sizeof(d1) / sizeof(d1[0]));
    } else if (voltage_mV < 20.644f) {
        temperature = s_poly_eval(voltage_mV, d2, sizeof(d2) / sizeof(d2[0]));
    } else {
        temperature = s_poly_eval(voltage_mV, d3, sizeof(d3) / sizeof(d3[0]));
    }
    return temperature;
}

esp_err_t thermocouple_init(thermocouple_t *thermocouple, const thermocouple_config_t *thermocouple_cfg) {
    if (thermocouple == NULL || thermocouple_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_config_t sensor_cfg = {
        .adc = thermocouple_cfg->adc,
        .p_pin = thermocouple_cfg->p_pin,
        .n_pin = thermocouple_cfg->n_pin,
        .gain = 64,
        .pga_enabled = true,
    };

    ESP_RETURN_ON_ERROR(
        sensor_init(&thermocouple->sensor, &sensor_cfg), TAG, "Failed to initialize thermocouple sensor"
    );

    thermocouple->unit = thermocouple_cfg->unit;
    return ESP_OK;
}

esp_err_t get_thermocouple_reading(thermocouple_t *thermocouple, float *temperature) {
    float voltage = 0;
    ESP_RETURN_ON_ERROR(
        sensor_voltage_reading(&thermocouple->sensor, &voltage), TAG, "Failed to get thermocouple voltage reading"
    );

    float cjc_temp = 0;
    ESP_RETURN_ON_ERROR(
        ads112c04_get_single_temperature_reading(thermocouple->sensor.adc, &cjc_temp),
        TAG,
        "Failed to get CJC temperature reading"
    );

    const float cjc_voltage_mV = s_temperature_to_voltage(cjc_temp);
    const float thermocouple_voltage_mV = voltage * 1000 + cjc_voltage_mV;
    const float temp_C = s_voltage_to_temperature(thermocouple_voltage_mV);

    if (thermocouple->unit == THERMOCOUPLE_C) {
        *temperature = temp_C;
    } else if (thermocouple->unit == THERMOCOUPLE_K) {
        *temperature = temp_C + 273.15;
    } else if (thermocouple->unit == THERMOCOUPLE_F) {
        *temperature = (temp_C * 1.8) + 32;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
