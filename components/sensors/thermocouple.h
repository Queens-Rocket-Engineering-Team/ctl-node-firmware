#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor.h"

typedef enum : uint8_t {
    THERMOCOUPLE_C,
    THERMOCOUPLE_K,
    THERMOCOUPLE_F,
} thermocouple_unit_t;

typedef struct {
    sensor_t sensor;
    thermocouple_unit_t unit;
} thermocouple_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t p_pin;
    ads112c04_pin_t n_pin;
    thermocouple_unit_t unit;
} thermocouple_config_t;

esp_err_t thermocouple_init(thermocouple_t *thermocouple, const thermocouple_config_t *thermocouple_cfg);

esp_err_t get_thermocouple_reading(thermocouple_t *thermocouple, float *temperature);
