#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t p_pin;
    ads112c04_pin_t n_pin;
    uint8_t gain;
    bool pga_enabled;
} sensor_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t p_pin;
    ads112c04_pin_t n_pin;
    uint8_t gain;
    bool pga_enabled;
} sensor_config_t;

esp_err_t sensor_init(sensor_t *sensor, const sensor_config_t *sensor_cfg);

esp_err_t sensor_voltage_reading(sensor_t *sensor, float *voltage);
