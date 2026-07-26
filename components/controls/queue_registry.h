#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>
#include <esp_err.h>

esp_err_t queue_registry_register(QueueHandle_t queue, uint8_t id);

esp_err_t queue_registry_get(QueueHandle_t *queue, uint8_t id);
