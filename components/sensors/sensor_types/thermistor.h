#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor_base.h"

typedef struct {
    sensor_base_t base;
    float beta;
    float resistor_ohms;
} thermistor_t;

typedef struct {
    ads112c04_t *adc;
    ads112c04_pin_t pin;
    float beta;
    float resistor_ohms;
    sensor_unit_t unit;
} thermistor_config_t;

esp_err_t thermistor_init(thermistor_t *thermistor, const thermistor_config_t *thermistor_cfg);

esp_err_t get_thermistor_reading(thermistor_t *thermistor, float *temperature);
