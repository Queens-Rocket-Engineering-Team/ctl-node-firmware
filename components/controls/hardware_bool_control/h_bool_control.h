#pragma once

#include <driver/gpio.h>
#include <esp_err.h>
#include <stdint.h>

#include "control_base.h"

typedef enum : uint8_t {
    BOOL_CONTROL_OPEN,
    BOOL_CONTROL_CLOSED,
    BOOL_CONTROL_UNKNOWN,
} h_bool_control_state_t;

typedef struct {
    control_base_t base;
    gpio_num_t gpio_num;
    h_bool_control_state_t state;
    h_bool_control_state_t default_state;
} h_bool_control_t;

typedef struct {
    gpio_num_t gpio_num;
    h_bool_control_state_t default_state;
} h_bool_control_config_t;

// Initialize control pin
esp_err_t h_bool_control_init(h_bool_control_t *control, const h_bool_control_config_t *control_cfg);

esp_err_t h_bool_control_open(h_bool_control_t *control);
esp_err_t h_bool_control_close(h_bool_control_t *control);
esp_err_t h_bool_control_set_default(h_bool_control_t *control);

h_bool_control_state_t h_bool_control_get_state(const h_bool_control_t *control);
