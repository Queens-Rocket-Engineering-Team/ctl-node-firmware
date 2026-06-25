#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor.h"

typedef enum : uint8_t {
    CURRENT_SENSOR_A,
} current_sensor_unit_t;

typedef struct {
    sensor_t sensor;
    float shunt_resistor_ohms;
    float csa_gain;
    current_sensor_unit_t unit;
} current_sensor_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t pin;
    float shunt_resistor_ohms;
    float csa_gain;
    current_sensor_unit_t unit;
} current_sensor_config_t;

esp_err_t current_sensor_init(current_sensor_t *current_sensor, const current_sensor_config_t *current_sensor_cfg);

esp_err_t get_current_reading(current_sensor_t *current_sensor, float *current);
