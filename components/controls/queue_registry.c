#include <freertos/FreeRTOS.h>
#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

#include "queue_registry.h"

#define MAX_REGISTERED_QUEUES 16

typedef struct {
    uint8_t id;
    QueueHandle_t queue_handle;
} registered_queue_handle_t;

static registered_queue_handle_t registered_queues[MAX_REGISTERED_QUEUES] = {0};
static uint8_t queues_registered = 0;

esp_err_t queue_registry_register(QueueHandle_t queue, uint8_t id) {
    if (queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // check for duplicate id
    for (size_t i = 0; i < queues_registered; i++) {
        if (registered_queues[i].id == id) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    const registered_queue_handle_t new_queue = {
        .id = id,
        .queue_handle = queue,
    };

    if (queues_registered < MAX_REGISTERED_QUEUES) {
        registered_queues[queues_registered] = new_queue;
        queues_registered++;
        return ESP_OK;
    }

    return ESP_ERR_NO_MEM;
}

esp_err_t queue_registry_get(QueueHandle_t *queue, uint8_t id) {
    if (queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < queues_registered; i++) {
        if (registered_queues[i].id == id) {
            *queue = registered_queues[i].queue_handle;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}











