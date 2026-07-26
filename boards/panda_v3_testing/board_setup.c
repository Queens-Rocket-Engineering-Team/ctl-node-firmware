#include <esp_err.h>
#include <freertos/FreeRTOS.h>

#include "board_setup.h"
#include "heater_control.h"

#define HEATER_STACK_SIZE 2048

esp_err_t board_setup() {

    static StaticTask_t xTaskBuffer_HEATER1;
    static StackType_t xStack_HEATER1[HEATER_STACK_SIZE];

    xTaskCreateStatic(
        heater_control_task,
        "Heater 1 Task",
        HEATER_STACK_SIZE,
        NULL,
        1,
        xStack_HEATER1,
        &xTaskBuffer_HEATER1
    );

    return ESP_OK;
}