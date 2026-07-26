#pragma once

#include <freertos/FreeRTOS.h>
#include <esp_err.h>
#include <stdint.h>

#include "queue_registry.h"
#include "control_base.h"

typedef struct {
    control_base_t base;
    QueueHandle_t control_queue;
    uint32_t state;
    uint32_t default_state;
} v_uint32_control_t;

typedef struct {
    uint8_t queue_id;
    uint32_t default_state;
} v_uint32_control_config_t;

esp_err_t v_uint32_control_init(v_uint32_control_t *control, const v_uint32_control_config_t *control_cfg);

esp_err_t v_uint32_control_set(v_uint32_control_t *control, uint32_t new_val);

esp_err_t v_uint32_control_set_default(v_uint32_control_t *control);

uint32_t v_uint32_control_get_state(v_uint32_control_t *control);