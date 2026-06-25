#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor_base.h"

typedef struct {
    sensor_base_t base;
    float resistor_ohms;
    float max_pressure_psi;
} pressure_transducer_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t pin;
    float resistor_ohms;
    float max_pressure_psi;
    sensor_unit_t unit;
} pressure_transducer_config_t;

esp_err_t pressure_transducer_init(
    pressure_transducer_t *pressure_transducer,
    const pressure_transducer_config_t *pressure_transducer_cfg
);

esp_err_t get_pressure_reading(pressure_transducer_t *pressure_transducer, float *pressure);
