#include <freertos/FreeRTOS.h>
#include <esp_check.h>
#include <esp_err.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>

#include "control.h"
#include "pid.h"
#include "fan_control.h"

static const char *TAG = "FAN CONTROL";

#define LEDC_MODE LEDC_LOW_SPEED_MODE

#define PWM_HZ 25000

#define PID_SAMPLE_RATE_MS 100
#define AVG_WINDOW_MS 1000
#define PCNT_HISTORY_LENGTH (AVG_WINDOW_MS / PID_SAMPLE_RATE_MS)

// these constants assume an error in units of RPM and a normalized output from 0 to 1 for duty cycle
#define PID_KP 1
#define PID_KI 0
#define PID_KD 0

esp_err_t fan_control_init(fan_ctx_t *fan_ctx) {
    // create a queue for the setpoint and register it in the queue registry

    QueueHandle_t fan_queue_handle = xQueueCreateStatic(
        FAN_QUEUE_LENGTH, FAN_ITEM_SIZE, fan_ctx->ucQueueStorageArea, &fan_ctx->xStaticQueue
    );

    esp_err_t err = queue_registry_register(fan_queue_handle, fan_ctx->queue_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register fan into queue registry, err: %s", esp_err_to_name(err));
    }
    return err;
}

void fan_control_task(void *pvParams) {
    fan_ctx_t *fan_ctx = (fan_ctx_t *) pvParams;

    QueueHandle_t fan_queue_handle = {0};
    queue_registry_get(&fan_queue_handle, fan_ctx->queue_id);

    // initialize the pwm driver
    ledc_timer_config_t fan_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = fan_ctx->timer,
        .freq_hz = PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_timer_config(&fan_timer));

    ledc_channel_config_t fan_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = fan_ctx->channel,
        .timer_sel      = fan_ctx->timer,
        .gpio_num       = fan_ctx->pwm_pin,
        .duty           = 0, // set duty to 0%
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_channel_config(&fan_channel));

    // set up pulse counter for tachometer
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,
        .low_limit = -32768,
        .flags.accum_count = 1,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt_new_unit(&unit_config, &pcnt_unit));

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 10000,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = fan_ctx->tach_pin,
        .level_gpio_num = -1, // disable level sensing for directionality
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD); // increase count on rising edges
    pcnt_channel_set_level_action(pcnt_chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP); // level pin is disabled

    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_start(pcnt_unit);

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
    uint32_t duty_cycle_10_bit = 0;

    int current_count = 0;
    int last_count = 0;
    uint32_t delta_count = 0;
    float reading = 0;

    uint32_t delta_history[PCNT_HISTORY_LENGTH] = {0};
    uint32_t delta_sum = 0;
    size_t pcnt_history_idx = 0;
    size_t window_fill_count = 0; // Used to prevent false low readings on startup

    TickType_t last_wake_time = xTaskGetTickCount();

    // control loop
    while (1) {
        // recieve a new setpoint from queue
        uint32_t new_setpoint = 0;
        if (xQueueReceive(fan_queue_handle, &new_setpoint, 0) == pdTRUE) {
            pid.setpoint = new_setpoint;
            pid.integral = 0.0f; // reset integral on new setpoint
            ESP_LOGI(TAG, "New setpoint: %u", new_setpoint);
        }

        // get the RPM reading
        pcnt_unit_get_count(pcnt_unit, &current_count);
        delta_count = (uint32_t)(current_count - last_count);
        last_count = current_count;

        // take a moving average with ring buffer
        delta_sum -= delta_history[pcnt_history_idx];
        delta_history[pcnt_history_idx] = delta_count;
        delta_sum += delta_count;
        
        pcnt_history_idx = (pcnt_history_idx + 1) % PCNT_HISTORY_LENGTH;
        
        // maintains accurate readings at startup
        if (window_fill_count < PCNT_HISTORY_LENGTH) {
            window_fill_count++;
        }
        float active_window_time = window_fill_count * (PID_SAMPLE_RATE_MS / 1000.0f);
        
        // find the rpm from the averaged value
        reading = (((float) delta_sum) / active_window_time) * 60.0f / 2.0f;

        // find duty cycle from pid controller
        duty_cycle = pid_step(&pid, reading);
        // clamp pwm duty cycle to avoid short pulses to fet
        if (duty_cycle > 0.99f) {
            duty_cycle = 1.0f;
        } else if (duty_cycle < 0.01f) {
            duty_cycle = 0.0f;
        }

        // update pwm duty cycle
        duty_cycle_10_bit = roundf(1023 * duty_cycle);
        if (duty_cycle_10_bit > 1023) {
            duty_cycle_10_bit = 1023;
        }
        ledc_set_duty(LEDC_MODE, fan_ctx->channel, duty_cycle_10_bit);
        ledc_update_duty(LEDC_MODE, fan_ctx->channel);

        // delay until next loop
        xTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(PID_SAMPLE_RATE_MS));
    }
}