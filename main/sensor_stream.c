#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
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

#define MAX_FREQUENCY 100

static const char *TAG = "SENSOR STREAM";

static esp_err_t s_generic_read_sensor(config_sensor_t *generic_sensor, float *value, uint8_t *unit) {
    if (generic_sensor == NULL || value == NULL || unit == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (generic_sensor->sensor_type) {
    case THERMOCOUPLE:
        switch (generic_sensor->sensor.thermocouple.unit) {
        case THERMOCOUPLE_C:
            *unit = QLCP_UNIT_CELSIUS;
            break;
        case THERMOCOUPLE_K:
            *unit = QLCP_UNIT_KELVIN;
            break;
        case THERMOCOUPLE_F:
            *unit = QLCP_UNIT_FAHRENHEIT;
            break;
        }
        ESP_RETURN_ON_ERROR(
            get_thermocouple_reading(&generic_sensor->sensor.thermocouple, value),
            TAG,
            "Failed to get thermocouple reading"
        );
        break;
    case PRESSURE_TRANSDUCER:
        switch (generic_sensor->sensor.pressure_transducer.unit) {
        case PRESSURE_TRANSDUCER_PSI:
            *unit = QLCP_UNIT_PSI;
            break;
        case PRESSURE_TRANSDUCER_BAR:
            *unit = QLCP_UNIT_BAR;
            break;
        case PRESSURE_TRANSDUCER_PA:
            *unit = QLCP_UNIT_PASCAL;
            break;
        }
        ESP_RETURN_ON_ERROR(
            get_pressure_reading(&generic_sensor->sensor.pressure_transducer, value),
            TAG,
            "Failed to get pressure transducer reading"
        );
        break;
    case LOAD_CELL:
        switch (generic_sensor->sensor.load_cell.unit) {
        case LOAD_CELL_KG:
            *unit = QLCP_UNIT_KILOGRAMS;
            break;
        case LOAD_CELL_N:
            *unit = QLCP_UNIT_NEWTONS;
            break;
        }
        ESP_RETURN_ON_ERROR(
            get_load_cell_reading(&generic_sensor->sensor.load_cell, value), TAG, "Failed to get load cell reading"
        );
        break;
    case RESISTANCE_SENSOR:
        switch (generic_sensor->sensor.resistance_sensor.unit) {
        case RESISTANCE_SENSOR_OHMS:
            *unit = QLCP_UNIT_OHMS;
            break;
        }
        ESP_RETURN_ON_ERROR(
            get_resistance_reading(&generic_sensor->sensor.resistance_sensor, value),
            TAG,
            "Failed to get resistance reading"
        );
        break;
    case CURRENT_SENSOR:
        switch (generic_sensor->sensor.current_sensor.unit) {
        case CURRENT_SENSOR_A:
            *unit = QLCP_UNIT_AMPS;
            break;
        }
        ESP_RETURN_ON_ERROR(
            get_current_reading(&generic_sensor->sensor.current_sensor, value), TAG, "Failed to get current reading"
        );
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

void sensor_stream(void *pvParams) {
    app_ctx_t *app_ctx = (app_ctx_t *)pvParams;

    esp_err_t err;

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

        static qlcp_sensor_data data[CONFIG_NUM_SENSORS] = {0};

        for (size_t i = 0; i < CONFIG_NUM_SENSORS; i++) {
            data[i].sensor_id = i;
            err = s_generic_read_sensor(&app_ctx->sensors[i], &data[i].value, &data[i].unit);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed sensor read for sensor index %u", i);
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
        // binary semaphore to prevent modifying data struct before sent
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
