#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor_base.h"

typedef struct {
    sensor_base_t base;
    uint16_t injected_current_uA;
    float r_short;
} resistance_sensor_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t pin;
    uint16_t injected_current_uA;
    float r_short;
    sensor_unit_t unit;
} resistance_sensor_config_t;

esp_err_t resistance_sensor_init(
    resistance_sensor_t *resistance_sensor,
    const resistance_sensor_config_t *resistance_sensor_cfg
);

esp_err_t get_resistance_reading(resistance_sensor_t *resistance_sensor, float *weight);