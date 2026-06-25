#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor_base.h"

typedef struct {
    sensor_base_t base;
    float load_rating_N;
    float full_scale_V;
} load_cell_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t p_pin;
    ads112c04_pin_t n_pin;
    float load_rating_N;
    float excitation_V;
    float sensitivity_vV;
    sensor_unit_t unit;
} load_cell_config_t;

esp_err_t load_cell_init(load_cell_t *load_cell, const load_cell_config_t *load_cell_cfg);

esp_err_t get_load_cell_reading(load_cell_t *load_cell, float *weight);
