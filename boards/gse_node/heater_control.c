#include <freertos/FreeRTOS.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_log.h>

#include "control.h"
#include "heater_control.h"

static const char *TAG = "HEATER CONTROL";

#define QUEUE_LENGTH 1
#define ITEM_SIZE sizeof(uint32_t)

void heater_control_task(void *pvParams) {

    StaticQueue_t xStaticQueue;
    uint8_t ucQueueStorageArea[QUEUE_LENGTH * ITEM_SIZE];

    QueueHandle_t heater_queue_handle = xQueueCreateStatic(
        QUEUE_LENGTH, ITEM_SIZE, ucQueueStorageArea, &xStaticQueue
    );

    esp_err_t err = queue_registry_register(heater_queue_handle, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register heater into queue registry, err: %s", esp_err_to_name(err));
    }

    while (1) {
        // testing queue recieve
        uint32_t new_setpoint = 0;
        xQueueReceive(heater_queue_handle, &new_setpoint, portMAX_DELAY);
        ESP_LOGI(TAG, "New setpoint: %u", new_setpoint);
    }
}