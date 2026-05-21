#pragma once

#include <driver/gpio.h>
#include <esp_err.h>
#include <stdint.h>

typedef enum : uint8_t {
    CONTROL_OPEN,
    CONTROL_CLOSED,
    CONTROL_UNKNOWN,
} control_state_t;

typedef struct {
    gpio_num_t gpio_num;
    control_state_t state;
    control_state_t default_state;
} control_t;

typedef struct {
    gpio_num_t gpio_num;
    control_state_t default_state;
} control_config_t;

// Initialize control pin
esp_err_t control_init(control_t *control, const control_config_t *control_cfg);

esp_err_t control_open(control_t *control);
esp_err_t control_close(control_t *control);
esp_err_t control_set_default(control_t *control);

control_state_t control_get_state(const control_t *control);
