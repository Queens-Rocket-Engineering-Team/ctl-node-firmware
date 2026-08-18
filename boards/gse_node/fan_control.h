#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>

#define FAN_QUEUE_LENGTH 1
#define FAN_ITEM_SIZE sizeof(uint32_t)

typedef struct {
    uint8_t queue_id;
    gpio_num_t pwm_pin;
    gpio_num_t tach_pin;
    ledc_timer_t timer;
    ledc_channel_t channel;
    StaticQueue_t xStaticQueue;
    uint8_t ucQueueStorageArea[FAN_QUEUE_LENGTH * FAN_ITEM_SIZE];
} fan_ctx_t;

esp_err_t fan_control_init(fan_ctx_t *fan_ctx);

void fan_control_task(void *pvParams);