#include <esp_err.h>
#include <esp_check.h>
#include <freertos/FreeRTOS.h>
#include <ads112c04.h>

#include "board_setup.h"
#include "heater_control.h"

#define HEATER_STACK_SIZE 4096

static const char *TAG = "BOARD SETUP";

static esp_err_t make_thermistor(thermistor_t *thermistor, ads112c04_t *adcs, size_t num_adcs, uint8_t addr, ads112c04_pin_t pin) {
    if (thermistor == NULL || adcs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ads112c04_t *adc = NULL;
    for (size_t i = 0; i < num_adcs; i++) {
        if (ads112c04_get_address(&adcs[i]) == addr) {
            adc = &adcs[i];
            break;
        }
    }

    thermistor_config_t thermistor_cfg = {
        .adc = adc,
        .pin = pin,
        .beta = 3950,
        .resistor_ohms = 10000,
        .unit = SENSOR_UNIT_C,
    };

    return thermistor_init(thermistor, &thermistor_cfg);
}

esp_err_t board_setup(board_ctx_t *board_ctx) {

    static thermistor_t thermistors[4] = {0};

    ESP_RETURN_ON_ERROR(make_thermistor(&thermistors[0], board_ctx->adcs, board_ctx->num_adcs, 68, ADS112C04_AIN0), TAG, "Failed heater 1, thermistor 0");
    ESP_RETURN_ON_ERROR(make_thermistor(&thermistors[1], board_ctx->adcs, board_ctx->num_adcs, 68, ADS112C04_AIN1), TAG, "Failed heater 1, thermistor 1");
    ESP_RETURN_ON_ERROR(make_thermistor(&thermistors[2], board_ctx->adcs, board_ctx->num_adcs, 68, ADS112C04_AIN2), TAG, "Failed heater 1, thermistor 2");
    ESP_RETURN_ON_ERROR(make_thermistor(&thermistors[3], board_ctx->adcs, board_ctx->num_adcs, 68, ADS112C04_AIN3), TAG, "Failed heater 1, thermistor 3");

    static heater_ctx_t heater_1_ctx = {
        .queue_id = 1,
        .thermistors = thermistors,
        .num_thermistors = 4,
        .pwm_pin = 14,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
    };

    ESP_RETURN_ON_ERROR(heater_control_init(&heater_1_ctx), TAG, "Failed to initialize heater");

    static StaticTask_t xTaskBuffer_HEATER1;
    static StackType_t xStack_HEATER1[HEATER_STACK_SIZE];

    xTaskCreateStatic(
        heater_control_task,
        "Heater 1 Task",
        HEATER_STACK_SIZE,
        (void *) &heater_1_ctx,
        1,
        xStack_HEATER1,
        &xTaskBuffer_HEATER1
    );

    return ESP_OK;
}