#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor_base.h"

typedef struct {
    sensor_base_t base;
    float injected_current_A;
    float preprocessing_gain;
} ign_driver_res_sensor_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t p_pin;
    ads112c04_pin_t n_pin;
    float injected_current_mA;
    float preprocessing_gain;
    sensor_unit_t unit;
} ign_driver_res_sensor_config_t;

esp_err_t ign_driver_res_sensor_init(
    ign_driver_res_sensor_t *resistance_sensor,
    const ign_driver_res_sensor_config_t *resistance_sensor_cfg
);

esp_err_t get_ign_driver_res_reading(ign_driver_res_sensor_t *resistance_sensor, float *resistance);