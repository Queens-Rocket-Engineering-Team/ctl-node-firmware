#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor_base.h"

typedef enum {
    THERMOCOUPLE_TYPE_K,
} thermocouple_type_t;

typedef struct {
    sensor_base_t base;
    thermocouple_type_t type;
} thermocouple_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t p_pin;
    ads112c04_pin_t n_pin;
    sensor_unit_t unit;
    thermocouple_type_t type;
} thermocouple_config_t;

esp_err_t thermocouple_init(thermocouple_t *thermocouple, const thermocouple_config_t *thermocouple_cfg);

esp_err_t get_thermocouple_reading(thermocouple_t *thermocouple, float *temperature);
