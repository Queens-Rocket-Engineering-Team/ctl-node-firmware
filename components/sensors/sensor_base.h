#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"

typedef enum {
    SENSOR_UNIT_UNITLESS,
    SENSOR_UNIT_A,
    SENSOR_UNIT_N,
    SENSOR_UNIT_KG,
    SENSOR_UNIT_PSI,
    SENSOR_UNIT_BAR,
    SENSOR_UNIT_PA,
    SENSOR_UNIT_OHMS,
    SENSOR_UNIT_C,
    SENSOR_UNIT_K,
    SENSOR_UNIT_F,
} sensor_unit_t;

typedef struct sensor_base sensor_base_t;

struct sensor_base {
    esp_err_t (*read_sensor)(sensor_base_t *base, float *value);
    ads112c04_t *adc;
    ads112c04_pin_t p_pin;
    ads112c04_pin_t n_pin;
    uint8_t gain;
    bool pga_enabled;
    sensor_unit_t unit;
};

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t p_pin;
    ads112c04_pin_t n_pin;
    uint8_t gain;
    bool pga_enabled;
    sensor_unit_t unit;
} sensor_base_config_t;

esp_err_t sensor_base_init(sensor_base_t *sensor_base, const sensor_base_config_t *sensor_base_cfg);

esp_err_t sensor_base_voltage_reading(sensor_base_t *sensor_base, float *voltage);