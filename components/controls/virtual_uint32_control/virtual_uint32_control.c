#include <freertos/FreeRTOS.h>
#include <stdint.h>
#include <esp_check.h>

#include "virtual_uint32_control.h"

static const char *TAG = "V_UINT32 CONTROL";

static esp_err_t s_set_default(control_base_t *base) {
    return v_uint32_control_set_default((v_uint32_control_t *) base);
}

esp_err_t v_uint32_control_init(v_uint32_control_t *control, const v_uint32_control_config_t *control_cfg) {
    if (control == NULL || control_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(queue_registry_get(&control->control_queue, control_cfg->queue_id), TAG, "Failed to find queue in registry");
    control->default_state = control_cfg->default_state;

    if (xQueueOverwrite(control->control_queue, (void *) &control->default_state) != pdTRUE) {
        return ESP_FAIL;
    }

    control->state = control->default_state;
    control->base.set_default = s_set_default;
    
    return ESP_OK;
}

esp_err_t v_uint32_control_set(v_uint32_control_t *control, uint32_t new_state) {
    if (control == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xQueueOverwrite(control->control_queue, (void *) &new_state) != pdTRUE) {
        return ESP_FAIL;
    }

    control->state = new_state;
    return ESP_OK;
}

esp_err_t v_uint32_control_set_default(v_uint32_control_t *control) {
    if (control == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xQueueOverwrite(control->control_queue, (void *) &control->default_state) != pdTRUE) {
        return ESP_FAIL;
    }

    control->state = control->default_state;
    return ESP_OK;
}

uint32_t v_uint32_control_get_state(v_uint32_control_t *control) {
    return control->state;
}
