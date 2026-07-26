#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_err.h>
#include <stdbool.h>

#include "h_bool_control.h"

static const char *TAG = "BOOL CONTROL";

static esp_err_t s_set_default(control_base_t *base) {
    return h_bool_control_set_default((h_bool_control_t *) base);
}

static esp_err_t s_bool_control_set_state(h_bool_control_t *control, h_bool_control_state_t new_state) {
    if (control == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (new_state == BOOL_CONTROL_UNKNOWN) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t level;
    if (control->default_state == BOOL_CONTROL_OPEN) { // choose correct gpio level for desired state
        level = (new_state == BOOL_CONTROL_CLOSED);    // ex. if new_state is closed, level must be high
    } else if (control->default_state == BOOL_CONTROL_CLOSED) {
        level = (new_state == BOOL_CONTROL_OPEN);
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(control->gpio_num, level), TAG, "Failed to set control GPIO level");

    control->state = new_state;
    return ESP_OK;
}

esp_err_t h_bool_control_init(h_bool_control_t *control, const h_bool_control_config_t *control_cfg) {
    if (control == NULL || control_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(control_cfg->gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(gpio_reset_pin(control_cfg->gpio_num), TAG, "Failed to reset GPIO");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << control_cfg->gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to configure GPIO");

    control->gpio_num = control_cfg->gpio_num;
    control->state = BOOL_CONTROL_UNKNOWN; // state will be if control_set_state succeeds
    control->default_state = control_cfg->default_state;

    ESP_RETURN_ON_ERROR(
        s_bool_control_set_state(control, control_cfg->default_state), TAG, "Failed to set default control state"
    );

    control->base.set_default = s_set_default;

    return ESP_OK;
}

esp_err_t h_bool_control_open(h_bool_control_t *control) {
    return s_bool_control_set_state(control, BOOL_CONTROL_OPEN);
}

esp_err_t h_bool_control_close(h_bool_control_t *control) {
    return s_bool_control_set_state(control, BOOL_CONTROL_CLOSED);
}

esp_err_t h_bool_control_set_default(h_bool_control_t *control) {
    return s_bool_control_set_state(control, control->default_state);
}

h_bool_control_state_t h_bool_control_get_state(const h_bool_control_t *control) {
    if (control == NULL) {
        return BOOL_CONTROL_UNKNOWN;
    }
    return control->state;
}