#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor_base.h"

typedef struct {
    sensor_base_t base;
    float sensitivity_mV_A;
} he_current_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t pin;
    float sensitivity_mV_A;
    sensor_unit_t unit;
} he_current_config_t;

esp_err_t he_current_init(he_current_t *current_sensor, const he_current_config_t *current_sensor_cfg);

esp_err_t get_he_current_reading(he_current_t *current_sensor, float *current);
