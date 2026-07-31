#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>

#include "thermistor.h"

#define HEATER_QUEUE_LENGTH 1
#define HEATER_ITEM_SIZE sizeof(uint32_t)

typedef struct {
    uint8_t queue_id;
    thermistor_t *thermistors;
    size_t num_thermistors;
    gpio_num_t pwm_pin;
    ledc_timer_t timer;
    ledc_channel_t channel;
    StaticQueue_t xStaticQueue;
    uint8_t ucQueueStorageArea[HEATER_QUEUE_LENGTH * HEATER_ITEM_SIZE];
} heater_ctx_t;

esp_err_t heater_control_init(heater_ctx_t *heater_ctx);

void heater_control_task(void *pvParams);



