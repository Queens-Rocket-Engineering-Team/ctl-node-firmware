#include <freertos/FreeRTOS.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_check.h>
#include <driver/ledc.h>
#include <math.h>
#include <stdbool.h>

#include "control.h"
#include "heater_control.h"
#include "pid.h"

static const char *TAG = "HEATER CONTROL";

#define LEDC_MODE LEDC_LOW_SPEED_MODE

#define PWM_HZ 10

#define PID_SAMPLE_RATE_MS 1000

// these constants assume an error in units of C and a normalized output from 0 to 1 for duty cycle, used with a 300w heater (STILL NEED TO BE TUNED)
#define PID_KP 1
#define PID_KI 0
#define PID_KD 0

esp_err_t heater_control_init(heater_ctx_t *heater_ctx) {
    // create a queue for the setpoint and register it in the queue registry

    QueueHandle_t heater_queue_handle = xQueueCreateStatic(
        HEATER_QUEUE_LENGTH, HEATER_ITEM_SIZE, heater_ctx->ucQueueStorageArea, &heater_ctx->xStaticQueue
    );

    esp_err_t err = queue_registry_register(heater_queue_handle, heater_ctx->queue_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register heater into queue registry, err: %s", esp_err_to_name(err));
    }
    return err;
}

void heater_control_task(void *pvParams) {
    heater_ctx_t *heater_ctx = (heater_ctx_t *) pvParams;

    QueueHandle_t heater_queue_handle = {0};
    queue_registry_get(&heater_queue_handle, heater_ctx->queue_id);

    // initialize the pwm driver
    ledc_timer_config_t heater_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = heater_ctx->timer,
        .freq_hz = PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_timer_config(&heater_timer));

    ledc_channel_config_t heater_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = heater_ctx->channel,
        .timer_sel      = heater_ctx->timer,
        .gpio_num       = heater_ctx->pwm_pin,
        .duty           = 0, // set duty to 0%
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_channel_config(&heater_channel));

    // set up pid controller
    pid_ctx_t pid = {
        .kp = PID_KP,
        .ki = PID_KI,
        .kd = PID_KD,
        .integral = 0,
        .setpoint = 0,
        .prev_measurement = 0,
        .dt = PID_SAMPLE_RATE_MS / 1000.0f,
        .output_max = 1,
        .output_min = 0,
    };

    float duty_cycle = 0;
    float reading = 0;
    float avg_reading = 0;
    uint32_t duty_cycle_14_bit = 0;

    TickType_t last_wake_time = xTaskGetTickCount();

    // control loop
    while (1) {
        // recieve a new setpoint from queue
        uint32_t new_setpoint = 0;
        if (xQueueReceive(heater_queue_handle, &new_setpoint, 0) == pdTRUE) {
            pid.setpoint = new_setpoint;
            pid.integral = 0.0f; // reset integral on new setpoint
            ESP_LOGI(TAG, "New setpoint: %u", new_setpoint);
        }

        // get the average temperature reading
        avg_reading = 0;
        for (size_t i = 0; i < heater_ctx->num_thermistors; i++) {
            get_thermistor_reading(&heater_ctx->thermistors[i], &reading);
            avg_reading += reading;
        }
        avg_reading /= heater_ctx->num_thermistors;

        // find duty cycle from pid controller
        duty_cycle = pid_step(&pid, avg_reading);
        // clamp pwm duty cycle to avoid short pulses to fet
        if (duty_cycle > 0.99f) {
            duty_cycle = 1.0f;
        } else if (duty_cycle < 0.01f) {
            duty_cycle = 0.0f;
        }

        // update pwm duty cycle
        duty_cycle_14_bit = roundf(16383 * duty_cycle);
        if (duty_cycle_14_bit > 16383) {
            duty_cycle_14_bit = 16383;
        }
        ledc_set_duty(LEDC_MODE, heater_ctx->channel, duty_cycle_14_bit);
        ledc_update_duty(LEDC_MODE, heater_ctx->channel);

        // delay until next loop
        xTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(PID_SAMPLE_RATE_MS));
    }
}