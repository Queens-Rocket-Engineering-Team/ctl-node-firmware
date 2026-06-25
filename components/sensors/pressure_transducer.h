#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor.h"

typedef enum : uint8_t {
    PRESSURE_TRANSDUCER_PSI,
    PRESSURE_TRANSDUCER_BAR,
    PRESSURE_TRANSDUCER_PA,
} pressure_transducer_unit_t;

typedef struct {
    sensor_t sensor;
    float resistor_ohms;
    float max_pressure_psi;
    pressure_transducer_unit_t unit;
} pressure_transducer_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t pin;
    float resistor_ohms;
    float max_pressure_psi;
    pressure_transducer_unit_t unit;
} pressure_transducer_config_t;

esp_err_t pressure_transducer_init(
    pressure_transducer_t *pressure_transducer,
    const pressure_transducer_config_t *pressure_transducer_cfg
);

esp_err_t get_pressure_reading(pressure_transducer_t *pressure_transducer, float *pressure);
