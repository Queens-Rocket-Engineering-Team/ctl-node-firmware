#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "config_json.h"
#include "control.h"
#include "qlcp_lib.h"
#include "sensor_stream.h"
#include "setup.h"
#include "wifi_tools.h"

#define WATCHDOG_RESET_TIMEOUT_MIN 5
#define WATCHDOG_RESET_TIMEOUT_US (WATCHDOG_RESET_TIMEOUT_MIN * 60 * 1000000ULL)

static const char *TAG = "MAIN";

void app_main(void) {

    static app_ctx_t app_ctx = {0};

    ESP_ERROR_CHECK(app_setup(&app_ctx));
    ESP_LOGI(TAG, "Setup complete");

    // reset watchdog timer
    uint64_t last_packet_time_us = esp_timer_get_time();
    // processing for incoming/outgoing packets
    qlcp_client_payload payload_in = {0};
    qlcp_server_payload payload_out = {0};

    while (1) {

        EventBits_t wifi_bits = xEventGroupGetBits(app_ctx.network_ctx->wifi_event_group_handle);
        EventBits_t stream_bits = xEventGroupGetBits(app_ctx.sensor_stream_event_group_handle);

        // disable data stream if disconnected
        if ((stream_bits & SENSOR_STREAM_ENABLE_BIT) && !(wifi_bits & SERVER_CONNECTED_BIT)) {
            xEventGroupClearBits(app_ctx.sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
            ESP_LOGI(TAG, "Sensor stream stopped");
        }
        // send config on connection
        if (!app_ctx.network_ctx->config_sent && (wifi_bits & SERVER_CONNECTED_BIT)) {
            last_packet_time_us = esp_timer_get_time();
            payload_out.packet_type = QLCP_PT_CONFIG;

            const uint16_t sequence = atomic_fetch_add(&app_ctx.sequence, 1);

            const qlcp_config_packet config = {
                .config_data = json_config_str,
                .config_data_len = JSON_CONFIG_LEN,
                .header = {
                           .sequence = sequence,
                           .timestamp = 0,
                           },
            };
            payload_out.payload_data.config = config;

            xQueueSend(app_ctx.network_ctx->tcp_send_queue_handle, (void *)&payload_out, 0);
            app_ctx.network_ctx->config_sent = true;
            ESP_LOGI(TAG, "Sent config to server");
        }
        // reset all controls to default state when watchdog timeout triggers
        if (esp_timer_get_time() - last_packet_time_us > WATCHDOG_RESET_TIMEOUT_US) {
            for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
                control_set_default(&app_ctx.controls[i]);
            }
            xEventGroupClearBits(app_ctx.sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
            ESP_LOGW(TAG, "Software watchdog triggered, reset to default state");
            last_packet_time_us = esp_timer_get_time();
        }
        // check for incoming packets from the tcp recv queue
        if (xQueueReceive(app_ctx.network_ctx->tcp_recv_queue_handle, &payload_in, pdMS_TO_TICKS(100)) == pdTRUE) {

            last_packet_time_us = esp_timer_get_time();

            switch (payload_in.packet_type) {
            case QLCP_PT_ESTOP:
                for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
                    control_set_default(&app_ctx.controls[i]);
                }
                xEventGroupClearBits(app_ctx.sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
                ESP_LOGW(TAG, "Received ESTOP, reset to default state");
                continue;

            case QLCP_PT_TIMESYNC: {
                const uint32_t new_ts_offset = payload_in.payload_data.header_only.timestamp -
                                               (uint32_t)(esp_timer_get_time() / 1000);
                atomic_store(&app_ctx.ts_offset, new_ts_offset);
                payload_out.packet_type = QLCP_PT_ACK;

                const uint32_t current_ts_offset = atomic_load(&app_ctx.ts_offset);
                const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
                const uint16_t sequence = atomic_fetch_add(&app_ctx.sequence, 1);

                const qlcp_ack_packet ack = {
                    .ack_packet_type = QLCP_PT_TIMESYNC,
                    .ack_sequence = payload_in.payload_data.header_only.sequence,
                    .header = {
                               .sequence = sequence,
                               .timestamp = timestamp,
                               },
                };
                payload_out.payload_data.ack = ack;
            } break;

            case QLCP_PT_CONTROL: {
                const uint8_t i = payload_in.payload_data.control.command_id;
                esp_err_t err = ESP_FAIL;
                qlcp_err_code nack_error_code = QLCP_ERR_HARDWARE_FAULT;

                if (i < CONFIG_NUM_CONTROLS) {
                    if (payload_in.payload_data.control.command_state == QLCP_CS_OPEN) {
                        err = control_open(&app_ctx.controls[i]);
                    } else if (payload_in.payload_data.control.command_state == QLCP_CS_CLOSED) {
                        err = control_close(&app_ctx.controls[i]);
                    }
                } else {
                    nack_error_code = QLCP_ERR_INVALID_PARAM;
                }

                ESP_ERROR_CHECK_WITHOUT_ABORT(err);
                if (err == ESP_OK) {
                    payload_out.packet_type = QLCP_PT_ACK;

                    const uint32_t current_ts_offset = atomic_load(&app_ctx.ts_offset);
                    const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
                    const uint16_t sequence = atomic_fetch_add(&app_ctx.sequence, 1);

                    const qlcp_ack_packet ack = {
                        .ack_packet_type = QLCP_PT_CONTROL,
                        .ack_sequence = payload_in.payload_data.header_only.sequence,
                        .header = {
                                   .sequence = sequence,
                                   .timestamp = timestamp,
                                   },
                    };
                    payload_out.payload_data.ack = ack;
                } else {
                    payload_out.packet_type = QLCP_PT_NACK;

                    const uint32_t current_ts_offset = atomic_load(&app_ctx.ts_offset);
                    const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
                    const uint16_t sequence = atomic_fetch_add(&app_ctx.sequence, 1);

                    const qlcp_nack_packet nack = {
                        .nack_packet_type = QLCP_PT_CONTROL,
                        .nack_sequence = payload_in.payload_data.header_only.sequence,
                        .nack_error_code = nack_error_code,
                        .header = {
                                   .sequence = sequence,
                                   .timestamp = timestamp,
                                   },
                    };
                    payload_out.payload_data.nack = nack;
                }
            } break;

            case QLCP_PT_STATUS_REQUEST: {
                static qlcp_control_data control_data[CONFIG_NUM_CONTROLS] = {0};

                for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
                    control_data[i].control_id = i;
                    const control_state_t control_internal_state = control_get_state(&app_ctx.controls[i]);
                    switch (control_internal_state) {
                    case CONTROL_OPEN:
                        control_data[i].control_state = QLCP_CS_OPEN;
                        break;
                    case CONTROL_CLOSED:
                        control_data[i].control_state = QLCP_CS_CLOSED;
                        break;
                    case CONTROL_UNKNOWN:
                        control_data[i].control_state = QLCP_CS_ERROR;
                    };
                }

                payload_out.packet_type = QLCP_PT_STATUS;

                const uint32_t current_ts_offset = atomic_load(&app_ctx.ts_offset);
                const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
                const uint16_t sequence = atomic_fetch_add(&app_ctx.sequence, 1);

                const qlcp_status_packet status = {
                    .control_data = control_data,
                    .control_count = CONFIG_NUM_CONTROLS,
                    .device_status = QLCP_DS_ACTIVE,
                    .header = {
                               .sequence = sequence,
                               .timestamp = timestamp,
                               },
                };
                payload_out.payload_data.status = status;
            } break;

            case QLCP_PT_STREAM_START: {
                // give the stream task the frequency
                xTaskNotify(
                    app_ctx.sensor_stream_handle,
                    payload_in.payload_data.stream_start.stream_frequency,
                    eSetValueWithOverwrite
                );
                // notify the stream task to start
                xEventGroupSetBits(app_ctx.sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
                ESP_LOGI(TAG, "Sensor stream started");
                payload_out.packet_type = QLCP_PT_ACK;

                const uint32_t current_ts_offset = atomic_load(&app_ctx.ts_offset);
                const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
                const uint16_t sequence = atomic_fetch_add(&app_ctx.sequence, 1);

                const qlcp_ack_packet ack = {
                    .ack_packet_type = QLCP_PT_STREAM_START,
                    .ack_sequence = payload_in.payload_data.header_only.sequence,
                    .header = {
                               .sequence = sequence,
                               .timestamp = timestamp,
                               },
                };
                payload_out.payload_data.ack = ack;
            } break;

            case QLCP_PT_STREAM_STOP: {
                xEventGroupClearBits(app_ctx.sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
                ESP_LOGI(TAG, "Sensor stream stopped");
                payload_out.packet_type = QLCP_PT_ACK;

                const uint32_t current_ts_offset = atomic_load(&app_ctx.ts_offset);
                const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
                const uint16_t sequence = atomic_fetch_add(&app_ctx.sequence, 1);

                const qlcp_ack_packet ack = {
                    .ack_packet_type = QLCP_PT_STREAM_STOP,
                    .ack_sequence = payload_in.payload_data.header_only.sequence,
                    .header = {
                               .sequence = sequence,
                               .timestamp = timestamp,
                               },
                };
                payload_out.payload_data.ack = ack;
            } break;

            case QLCP_PT_GET_SINGLE: {
                // notify the stream task to send single reading
                xEventGroupSetBits(app_ctx.sensor_stream_event_group_handle, SENSORS_SINGLE_READING_BIT);
                ESP_LOGI(TAG, "Sensors single reading");
            }
                continue;

            case QLCP_PT_HEARTBEAT: {
                payload_out.packet_type = QLCP_PT_ACK;

                const uint32_t current_ts_offset = atomic_load(&app_ctx.ts_offset);
                const uint32_t timestamp = current_ts_offset + (uint32_t)(esp_timer_get_time() / 1000);
                const uint16_t sequence = atomic_fetch_add(&app_ctx.sequence, 1);

                const qlcp_ack_packet ack = {
                    .ack_packet_type = QLCP_PT_HEARTBEAT,
                    .ack_sequence = payload_in.payload_data.header_only.sequence,
                    .header = {
                               .sequence = sequence,
                               .timestamp = timestamp,
                               },
                };
                payload_out.payload_data.ack = ack;
            } break;

            case QLCP_PT_ACK:
            case QLCP_PT_NACK:
                continue;

            default:
                ESP_LOGE(TAG, "Invalid client packet type recieved: %d", payload_in.packet_type);
                continue;
            }
            // send the packet out to the tcp send queue
            xQueueSend(app_ctx.network_ctx->tcp_send_queue_handle, (void *)&payload_out, MESSAGE_QUEUE_TIMEOUT);
        }
    }
}
