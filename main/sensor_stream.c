#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <float.h>
#include <freertos/FreeRTOS.h>
#include <stdatomic.h>

#include "ads112c04.h"
#include "sensor.h"

#include "config_json.h"
#include "qlcp_lib.h"
#include "sensor_stream.h"
#include "setup.h"
#include "wifi_tools.h"

#define SENSOR_READ_STACK_SIZE 2048
#define MAX_FREQUENCY 1000UL
#define RING_BUFFER_LEN (UDP_SEND_QUEUE_LEN + 2)
// +2 to account for packet taken out of queue by
// udp_send + extra packet to write to when queue is full

static const char *TAG = "SENSOR STREAM";

typedef struct {
    SemaphoreHandle_t sensor_read_done_semaphore;
    sensor_t *sensor;
    float value;
    uint8_t unit;
} sensor_ctx_t;

static void s_read_sensor_task(void *pvParams) {
    sensor_ctx_t *ctx = (sensor_ctx_t *)pvParams;

    while (1) {
        // wait until sensor read triggered
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // get sensor reading
        if (ctx->sensor->base.read_sensor(&ctx->sensor->base, &ctx->value) != ESP_OK) {
            ctx->value = FLT_MAX; // assign max float value on fail
        }

        // translate sensor unit to QLCP units
        switch (ctx->sensor->base.unit) {
        case SENSOR_UNIT_A:
            ctx->unit = QLCP_UNIT_AMPS;
            break;
        case SENSOR_UNIT_N:
            ctx->unit = QLCP_UNIT_NEWTONS;
            break;
        case SENSOR_UNIT_KG:
            ctx->unit = QLCP_UNIT_KILOGRAMS;
            break;
        case SENSOR_UNIT_PSI:
            ctx->unit = QLCP_UNIT_PSI;
            break;
        case SENSOR_UNIT_BAR:
            ctx->unit = QLCP_UNIT_BAR;
            break;
        case SENSOR_UNIT_PA:
            ctx->unit = QLCP_UNIT_PASCAL;
            break;
        case SENSOR_UNIT_OHMS:
            ctx->unit = QLCP_UNIT_OHMS;
            break;
        case SENSOR_UNIT_C:
            ctx->unit = QLCP_UNIT_CELSIUS;
            break;
        case SENSOR_UNIT_K:
            ctx->unit = QLCP_UNIT_KELVIN;
            break;
        case SENSOR_UNIT_F:
            ctx->unit = QLCP_UNIT_FAHRENHEIT;
            break;
        case SENSOR_UNIT_UNITLESS:
        default:
            ctx->unit = QLCP_UNIT_UNITLESS;
            break;
        }
        // give back semaphore on completion
        xSemaphoreGive(ctx->sensor_read_done_semaphore);
    }
}

static void s_stream_timer_callback(void *ctx) {
    SemaphoreHandle_t *stream_timer_semaphore = (SemaphoreHandle_t *)ctx;
    xSemaphoreGive(*stream_timer_semaphore);
}

void sensor_stream(void *pvParams) {
    app_ctx_t *app_ctx = (app_ctx_t *)pvParams;

    // outgoing packet buffer
    static qlcp_sensor_data data_ring_buffer[RING_BUFFER_LEN][CONFIG_NUM_SENSORS] = {0};
    // semaphore buffers to notify stream task
    static StaticSemaphore_t sensor_read_done_semaphore_buffer[CONFIG_NUM_SENSORS];
    // stack for static sensor read tasks
    static StaticTask_t sensor_read_task_buffer[CONFIG_NUM_SENSORS];
    static StackType_t sensor_read_stack[CONFIG_NUM_SENSORS][SENSOR_READ_STACK_SIZE];
    static TaskHandle_t sensor_read_task[CONFIG_NUM_SENSORS];
    // context for sensor read tasks
    static sensor_ctx_t sensor_ctx[CONFIG_NUM_SENSORS] = {0};

    // start sensor read tasks
    for (size_t i = 0; i < CONFIG_NUM_SENSORS; i++) {
        sensor_ctx[i].sensor_read_done_semaphore = xSemaphoreCreateBinaryStatic(&sensor_read_done_semaphore_buffer[i]);
        configASSERT(sensor_ctx[i].sensor_read_done_semaphore);
        sensor_ctx[i].sensor = &app_ctx->sensors[i];

        char task_name[25];
        snprintf(task_name, sizeof(task_name), "sensor_read_%u", i);

        sensor_read_task[i] = xTaskCreateStatic(
            s_read_sensor_task,
            task_name,
            SENSOR_READ_STACK_SIZE,
            (void *)&sensor_ctx[i],
            1,
            sensor_read_stack[i],
            &sensor_read_task_buffer[i]
        );
    }
    uint32_t data_idx = 0; // initialize data ring buffer index to 0

    // set up stream timer for frequency control
    StaticSemaphore_t stream_timer_semaphore_buffer;
    SemaphoreHandle_t stream_timer_semaphore = xSemaphoreCreateBinaryStatic(&stream_timer_semaphore_buffer);
    configASSERT(stream_timer_semaphore);

    uint64_t period_us = 1000000ULL; // default to 1Hz

    const esp_timer_create_args_t stream_timer_args = {
        .callback = &s_stream_timer_callback,
        .arg = &stream_timer_semaphore,
        .name = "stream timer"
    };
    esp_timer_handle_t stream_timer;
    esp_timer_create(&stream_timer_args, &stream_timer);
    esp_timer_start_periodic(stream_timer, period_us);

    while (1) {
        // wait for signal to start a reading
        xEventGroupWaitBits(
            app_ctx->sensor_stream_event_group_handle,
            SENSOR_STREAM_ENABLE_BIT | SENSORS_SINGLE_READING_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY
        );
        // use task notification to recieve an updated frequency if available
        uint32_t new_frequency;
        if (xTaskNotifyWait(0, UINT32_MAX, &new_frequency, 0) == pdTRUE) {
            if (new_frequency > 0) {
                const uint32_t limited_frequency = (new_frequency < MAX_FREQUENCY ? new_frequency : MAX_FREQUENCY);
                period_us = 1000000ULL / limited_frequency;
                ESP_LOGI(TAG, "Stream frequency: %u", limited_frequency);
                esp_timer_stop(stream_timer);
                esp_timer_start_periodic(stream_timer, period_us);
            }
        }
        // if single reading, clear bit to avoid relooping
        if (xEventGroupGetBits(app_ctx->sensor_stream_event_group_handle) & SENSORS_SINGLE_READING_BIT) {
            xEventGroupClearBits(app_ctx->sensor_stream_event_group_handle, SENSORS_SINGLE_READING_BIT);
        } else {
            xSemaphoreTake(stream_timer_semaphore, portMAX_DELAY);
        }

        // set pointer to current ring buffer position
        qlcp_sensor_data *data = data_ring_buffer[data_idx];

        // start individual sensor read tasks
        for (size_t i = 0; i < CONFIG_NUM_SENSORS; i++) {
            xTaskNotifyGive(sensor_read_task[i]);
        }

        // wait until all sensor reads are complete
        for (size_t i = 0; i < CONFIG_NUM_SENSORS; i++) {
            if (xSemaphoreTake(sensor_ctx[i].sensor_read_done_semaphore, pdMS_TO_TICKS(250)) == pdTRUE) {
                data[i].sensor_id = i;
                data[i].value = sensor_ctx[i].value;
                data[i].unit = sensor_ctx[i].unit;
            } else {
                data[i].sensor_id = i;
                data[i].value = FLT_MAX;
                data[i].unit = QLCP_UNIT_UNITLESS;
            }
        }

        // create data packet
        const uint32_t current_ts_offset = atomic_load(&app_ctx->ts_offset);
        const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
        const uint16_t sequence = atomic_fetch_add(&app_ctx->sequence, 1);

        qlcp_data_packet data_packet = {
            .sensor_data = data, // pointer to data buffer in ring buffer
            .sensor_count = CONFIG_NUM_SENSORS,
            .header = {
                       .sequence = sequence,
                       .timestamp = timestamp,
                       },
        };
        // send data packet to the udp send queue
        if (xQueueSend(app_ctx->network_ctx->udp_send_queue_handle, (void *)&data_packet, MESSAGE_QUEUE_TIMEOUT) == pdTRUE) {
            // if packet added to queue increment ring buffer index
            data_idx = (data_idx + 1) % RING_BUFFER_LEN;
        } // else drop packet and reuse index
    }
}
