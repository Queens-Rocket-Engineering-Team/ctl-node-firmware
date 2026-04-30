#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <float.h>
#include <freertos/FreeRTOS.h>
#include <stdatomic.h>

#include "ads112c04.h"
#include "current_sensor.h"
#include "load_cell.h"
#include "pressure_transducer.h"
#include "resistance_sensor.h"
#include "thermocouple.h"

#include "config_json.h"
#include "qlcp_lib.h"
#include "sensor_stream.h"
#include "setup.h"

#define SENSOR_READ_STACK_SIZE 4096
#define MAX_FREQUENCY 250

static const char *TAG = "SENSOR STREAM";

typedef struct {
    SemaphoreHandle_t sensor_read_trigger_semaphore;
    SemaphoreHandle_t sensor_read_done_semaphore;
    config_sensor_t *generic_sensor;
    float value;
    uint8_t unit;
} sensor_ctx_t;

static void s_generic_read_sensor(void *pvParams) {
    sensor_ctx_t *ctx = (sensor_ctx_t *)pvParams;

    while (1) {
        // wait until sensor read triggered
        xSemaphoreTake(ctx->sensor_read_trigger_semaphore, portMAX_DELAY);

        switch (ctx->generic_sensor->sensor_type) {
        case THERMOCOUPLE:
            switch (ctx->generic_sensor->sensor.thermocouple.unit) {
            case THERMOCOUPLE_C:
                ctx->unit = QLCP_UNIT_CELSIUS;
                break;
            case THERMOCOUPLE_K:
                ctx->unit = QLCP_UNIT_KELVIN;
                break;
            case THERMOCOUPLE_F:
                ctx->unit = QLCP_UNIT_FAHRENHEIT;
                break;
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                get_thermocouple_reading(&ctx->generic_sensor->sensor.thermocouple, &ctx->value)
            );
            break;
        case PRESSURE_TRANSDUCER:
            switch (ctx->generic_sensor->sensor.pressure_transducer.unit) {
            case PRESSURE_TRANSDUCER_PSI:
                ctx->unit = QLCP_UNIT_PSI;
                break;
            case PRESSURE_TRANSDUCER_BAR:
                ctx->unit = QLCP_UNIT_BAR;
                break;
            case PRESSURE_TRANSDUCER_PA:
                ctx->unit = QLCP_UNIT_PASCAL;
                break;
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                get_pressure_reading(&ctx->generic_sensor->sensor.pressure_transducer, &ctx->value)
            );
            break;
        case LOAD_CELL:
            switch (ctx->generic_sensor->sensor.load_cell.unit) {
            case LOAD_CELL_KG:
                ctx->unit = QLCP_UNIT_KILOGRAMS;
                break;
            case LOAD_CELL_N:
                ctx->unit = QLCP_UNIT_NEWTONS;
                break;
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(get_load_cell_reading(&ctx->generic_sensor->sensor.load_cell, &ctx->value));
            break;
        case RESISTANCE_SENSOR:
            switch (ctx->generic_sensor->sensor.resistance_sensor.unit) {
            case RESISTANCE_SENSOR_OHMS:
                ctx->unit = QLCP_UNIT_OHMS;
                break;
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                get_resistance_reading(&ctx->generic_sensor->sensor.resistance_sensor, &ctx->value)
            );
            break;
        case CURRENT_SENSOR:
            switch (ctx->generic_sensor->sensor.current_sensor.unit) {
            case CURRENT_SENSOR_A:
                ctx->unit = QLCP_UNIT_AMPS;
                break;
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(get_current_reading(&ctx->generic_sensor->sensor.current_sensor, &ctx->value));
            break;
        default:
            ctx->value = FLT_MAX; // assign max float value on fail
            ctx->unit = QLCP_UNIT_UNITLESS;
        }
        // give back semaphore on completion
        xSemaphoreGive(ctx->sensor_read_done_semaphore);
    }
}

void sensor_stream(void *pvParams) {
    app_ctx_t *app_ctx = (app_ctx_t *)pvParams;

    // outgoing packet buffer
    static qlcp_sensor_data data[CONFIG_NUM_SENSORS] = {0};
    // semaphore buffers to notify sensor read tasks
    static StaticSemaphore_t sensor_read_trigger_semaphore_buffer[CONFIG_NUM_SENSORS];
    static StaticSemaphore_t sensor_read_done_semaphore_buffer[CONFIG_NUM_SENSORS];
    // stack for static sensor read tasks
    static StaticTask_t sensor_read_task_buffer[CONFIG_NUM_SENSORS];
    static StackType_t sensor_read_stack[CONFIG_NUM_SENSORS][SENSOR_READ_STACK_SIZE];
    // context for sensor read tasks
    static sensor_ctx_t sensor_ctx[CONFIG_NUM_SENSORS] = {0};

    for (size_t i = 0; i < CONFIG_NUM_SENSORS; i++) {

        sensor_ctx[i].sensor_read_trigger_semaphore = xSemaphoreCreateBinaryStatic(&sensor_read_trigger_semaphore_buffer[i]);
        sensor_ctx[i].sensor_read_done_semaphore = xSemaphoreCreateBinaryStatic(&sensor_read_done_semaphore_buffer[i]);
        sensor_ctx[i].generic_sensor = &app_ctx->sensors[i];

        char task_name[25];
        snprintf(task_name, sizeof(task_name), "sensor_read_%u", i);

        xTaskCreateStatic(
            s_generic_read_sensor,
            task_name,
            SENSOR_READ_STACK_SIZE,
            (void *)&sensor_ctx[i],
            1,
            sensor_read_stack[i],
            &sensor_read_task_buffer[i]
        );
    }

    uint32_t period_ms = 1000; // default to 1Hz
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {

        xEventGroupWaitBits(
            app_ctx->sensor_stream_event_group_handle,
            SENSOR_STREAM_ENABLE_BIT | SENSORS_SINGLE_READING_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY
        );

        uint32_t new_frequency;
        if (xTaskNotifyWait(0, 0, &new_frequency, 0) == pdTRUE) {
            if (new_frequency > 0) {
                const uint32_t limited_frequency = (new_frequency < MAX_FREQUENCY ? new_frequency : MAX_FREQUENCY);
                period_ms = 1000 / limited_frequency;
                ESP_LOGI(TAG, "Stream frequency: %u", limited_frequency);
                xLastWakeTime = xTaskGetTickCount(); // update timestamp to avoid double sending packets on change
            }
        }
        // start individual sensor read tasks
        for (size_t i = 0; i < CONFIG_NUM_SENSORS; i++) {
            data[i].sensor_id = i;
            xSemaphoreGive(sensor_ctx[i].sensor_read_trigger_semaphore);
        }
        // wait until all sensor reads are complete
        for (size_t i = 0; i < CONFIG_NUM_SENSORS; i++) {
            if (xSemaphoreTake(sensor_ctx[i].sensor_read_done_semaphore, pdMS_TO_TICKS(250)) == pdTRUE) {
                data[i].value = sensor_ctx[i].value;
                data[i].unit = sensor_ctx[i].unit;
            } else {
                data[i].value = FLT_MAX;
                data[i].unit = QLCP_UNIT_UNITLESS;
            }
        }

        const uint32_t current_ts_offset = atomic_load(&app_ctx->ts_offset);
        const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
        const uint16_t sequence = atomic_fetch_add(&app_ctx->sequence, 1);

        qlcp_data_packet data_packet = {
            .sensor_data = data,
            .sensor_count = CONFIG_NUM_SENSORS,
            .header = {
                       .sequence = sequence,
                       .timestamp = timestamp,
                       },
        };
        // send data packets to the udp send queue
        xQueueSend(app_ctx->network_ctx->udp_send_queue_handle, (void *)&data_packet, MESSAGE_QUEUE_TIMEOUT);
        // binary semaphore to prevent proceeding before data sent
        xSemaphoreTake(app_ctx->network_ctx->udp_send_semaphore_handle, pdMS_TO_TICKS(50));

        // if single reading, clear bit to avoid relooping
        if (xEventGroupGetBits(app_ctx->sensor_stream_event_group_handle) & SENSORS_SINGLE_READING_BIT) {
            xEventGroupClearBits(app_ctx->sensor_stream_event_group_handle, SENSORS_SINGLE_READING_BIT);
        } else {
            // xTaskDelayUntil accounts for time taken to read sensors
            xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(period_ms));
        }
    }
}
