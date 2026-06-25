#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ads112c04.h"
#include "load_cell.h"
#include "sensor.h"

static const char *TAG = "LOAD CELL";

esp_err_t load_cell_init(load_cell_t *load_cell, const load_cell_config_t *load_cell_cfg) {
    if (load_cell == NULL || load_cell_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_config_t sensor_cfg = {
        .adc = load_cell_cfg->adc,
        .p_pin = load_cell_cfg->p_pin,
        .n_pin = load_cell_cfg->n_pin,
        .gain = 128,
        .pga_enabled = true,
    };

    ESP_RETURN_ON_ERROR(sensor_init(&load_cell->sensor, &sensor_cfg), TAG, "Failed to initialize load cell sensor");

    load_cell->load_rating_N = load_cell_cfg->load_rating_N;
    load_cell->full_scale_V = load_cell_cfg->excitation_V * (load_cell_cfg->sensitivity_vV / 1000);
    load_cell->unit = load_cell_cfg->unit;
    return ESP_OK;
}

esp_err_t get_load_cell_reading(load_cell_t *load_cell, float *weight) {
    if (load_cell == NULL || weight == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float voltage = 0;
    ESP_RETURN_ON_ERROR(
        sensor_voltage_reading(&load_cell->sensor, &voltage), TAG, "Failed to get load cell voltage reading"
    );

    const float newtons = (voltage / load_cell->full_scale_V) * load_cell->load_rating_N;
    if (load_cell->unit == LOAD_CELL_N) {
        *weight = newtons;
    } else if (load_cell->unit == LOAD_CELL_KG) {
        *weight = newtons / 9.805;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
