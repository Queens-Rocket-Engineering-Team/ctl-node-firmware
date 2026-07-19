#include <stdint.h>
#include <stdatomic.h>
#include <freertos/FreeRTOS.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <esp_log.h>

#include "sensor_stream.h"
#include "config_json.h"
#include "qlcp_lib.h"
#include "setup.h"
#include "control.h"

#define WATCHDOG_RESET_TIMEOUT_MIN 5
#define WATCHDOG_RESET_TIMEOUT_US (WATCHDOG_RESET_TIMEOUT_MIN * 60 * 1000000ULL)

static const char *TAG = "PACKET HANDLER";

// helpers

static qlcp_ack_packet make_ack_packet(uint8_t ack_sequence, qlcp_packet_type ack_type, app_ctx_t *app_ctx) {
    const int64_t current_ts_offset = atomic_load(&app_ctx->ts_offset);
    const int64_t timestamp_us = esp_timer_get_time() - current_ts_offset;
    const uint8_t sequence = atomic_fetch_add(&app_ctx->sequence, 1);

    const qlcp_ack_packet ack = {
        .ack_packet_type = ack_type,
        .ack_sequence = ack_sequence,
        .header = {
                   .sequence = sequence,
                   .timestamp_us = timestamp_us,
                   },
    };
    return ack;
}

static qlcp_nack_packet make_nack_packet(uint8_t nack_sequence, qlcp_packet_type nack_type, qlcp_err_code nack_err, app_ctx_t *app_ctx) {
    const int64_t current_ts_offset = atomic_load(&app_ctx->ts_offset);
    const int64_t timestamp_us = esp_timer_get_time() - current_ts_offset;
    const uint8_t sequence = atomic_fetch_add(&app_ctx->sequence, 1);

    const qlcp_nack_packet nack = {
        .nack_packet_type = nack_type,
        .nack_sequence = nack_sequence,
        .nack_error_code = nack_err,
        .header = {
                   .sequence = sequence,
                   .timestamp_us = timestamp_us,
                   },
    };
    return nack;
}


static qlcp_status_packet make_status_packet(uint8_t ack_sequence, qlcp_packet_type ack_type, app_ctx_t *app_ctx) {
    static qlcp_control_data control_data[CONFIG_NUM_CONTROLS] = {0};

    // right now this only handles bool controls, needs to be fixed
    for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
        control_data[i].id = i;
        control_data[i].type = QLCP_CONTROL_BOOL;
        const control_state_t control_internal_state = control_get_state(&app_ctx->controls[i]);
        switch (control_internal_state) {
        case CONTROL_OPEN:
            control_data[i].state.control_bool = QLCP_CS_OPEN;
            break;
        case CONTROL_CLOSED:
            control_data[i].state.control_bool = QLCP_CS_CLOSED;
            break;
        case CONTROL_UNKNOWN:
            control_data[i].state.control_bool = QLCP_CS_ERROR;
        };
    }

    const int64_t current_ts_offset = atomic_load(&app_ctx->ts_offset);
    const int64_t timestamp_us = esp_timer_get_time() - current_ts_offset;
    const uint8_t sequence = atomic_fetch_add(&app_ctx->sequence, 1);

    const qlcp_status_packet status = {
        .control_data = control_data,
        .control_count = CONFIG_NUM_CONTROLS,
        .ack_packet_type = ack_type,
        .ack_sequence = ack_sequence,
        .header = {
                   .sequence = sequence,
                   .timestamp_us = timestamp_us,
                   },
    };
    return status;
}

// packet handlers

static void estop_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *estop_packet, qlcp_server_payload *payload_out) {
    for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
        control_set_default(&app_ctx->controls[i]);
    }
    ESP_LOGW(TAG, "Received ESTOP, reset to default state");

    payload_out->packet_type = QLCP_PT_STATUS;
    payload_out->payload_data.status = make_status_packet(estop_packet->header.sequence, QLCP_PT_ESTOP, app_ctx);
}

static void timesync_resp_handler(app_ctx_t *app_ctx, qlcp_timesync_resp_packet *timesync_resp_packet, qlcp_server_payload *payload_out) {
    const uint64_t t1 = timesync_resp_packet->t1_echo_us;
    const uint64_t t2 = timesync_resp_packet->t2_us;
    const uint64_t t3 = timesync_resp_packet->header.timestamp_us;
    const uint64_t t4 = esp_timer_get_time();
    const uint64_t new_ts_offset = ((t1 - t2) + (t4 - t3)) / 2;
    atomic_store(&app_ctx->ts_offset, new_ts_offset);

    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = make_ack_packet(timesync_resp_packet->header.sequence, QLCP_PT_TIMESYNC_RESP, app_ctx);
}

static void control_handler(app_ctx_t *app_ctx, qlcp_control_packet *control_packet, qlcp_server_payload *payload_out) {
    const uint8_t i = control_packet->control_data.id;

    esp_err_t err = ESP_FAIL;
    qlcp_err_code nack_err = QLCP_ERR_HARDWARE_FAULT;

    // ensure the control index is valid
    if (i < CONFIG_NUM_CONTROLS) {
        switch (control_packet->control_data.type) {
            case QLCP_CONTROL_BOOL:
                if (control_packet->control_data.state.control_bool == QLCP_CS_OPEN) {
                    err = control_open(&app_ctx->controls[i]);
                } else if (control_packet->control_data.state.control_bool == QLCP_CS_CLOSED) {
                    err = control_close(&app_ctx->controls[i]);
                } else {
                    nack_err = QLCP_ERR_INVALID_PARAM;
                }
                break;
            // finish these when other controls are implemented
            case QLCP_CONTROL_UINT32:
            case QLCP_CONTROL_INT32:
            case QLCP_CONTROL_FLOAT32:
            default:
        }
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
    if (err == ESP_OK) {
        payload_out->packet_type = QLCP_PT_STATUS;
        payload_out->payload_data.status = make_status_packet(control_packet->header.sequence, QLCP_PT_CONTROL, app_ctx);
    } else {
        payload_out->packet_type = QLCP_PT_NACK;
        payload_out->payload_data.nack = make_nack_packet(control_packet->header.sequence, QLCP_PT_CONTROL, nack_err, app_ctx);
    }
}

static void status_request_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *status_request_packet, qlcp_server_payload *payload_out) {
    payload_out->packet_type = QLCP_PT_STATUS;
    payload_out->payload_data.status = make_status_packet(status_request_packet->header.sequence, QLCP_PT_STATUS_REQUEST, app_ctx);
}

static void stream_start_handler(app_ctx_t *app_ctx, qlcp_stream_start_packet *stream_start_packet, qlcp_server_payload *payload_out) {
    // give the stream task the frequency
    xTaskNotify(
        app_ctx->sensor_stream_handle,
        stream_start_packet->stream_frequency,
        eSetValueWithOverwrite
    );
    // notify the stream task to start
    xEventGroupSetBits(app_ctx->sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
    ESP_LOGI(TAG, "Sensor stream started");

    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = make_ack_packet(stream_start_packet->header.sequence, QLCP_PT_STREAM_START, app_ctx);
}

static void stream_stop_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *stream_stop_packet, qlcp_server_payload *payload_out) {
    xEventGroupClearBits(app_ctx->sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
    ESP_LOGI(TAG, "Sensor stream stopped");

    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = make_ack_packet(stream_stop_packet->header.sequence, QLCP_PT_STREAM_STOP, app_ctx);
}

static void get_single_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *get_single_packet, qlcp_server_payload *payload_out) {
    xEventGroupSetBits(app_ctx->sensor_stream_event_group_handle, SENSORS_SINGLE_READING_BIT);
    ESP_LOGI(TAG, "Sensors single reading");

    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = make_ack_packet(get_single_packet->header.sequence, QLCP_PT_GET_SINGLE, app_ctx);
}

static void heartbeat_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *heartbeat_packet, qlcp_server_payload *payload_out) {
    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = make_ack_packet(heartbeat_packet->header.sequence, QLCP_PT_HEARTBEAT, app_ctx);
}

// handler loop
void packet_handler(void *pvParams) {
    app_ctx_t *app_ctx = (app_ctx_t *) pvParams;
    
    // reset watchdog timer
    uint64_t last_packet_time_us = esp_timer_get_time();
    // processing for incoming/outgoing packets
    qlcp_client_payload payload_in = {0};
    qlcp_server_payload payload_out = {0};

    while (1) {

        EventBits_t wifi_bits = xEventGroupGetBits(app_ctx->network_ctx->wifi_event_group_handle);
        EventBits_t stream_bits = xEventGroupGetBits(app_ctx->sensor_stream_event_group_handle);

        // disable data stream if disconnected
        if ((stream_bits & SENSOR_STREAM_ENABLE_BIT) && !(wifi_bits & SERVER_CONNECTED_BIT)) {
            xEventGroupClearBits(app_ctx->sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
            ESP_LOGI(TAG, "Sensor stream stopped");
        }
        // send config on connection
        if (!app_ctx->network_ctx->config_sent && (wifi_bits & SERVER_CONNECTED_BIT)) {
            last_packet_time_us = esp_timer_get_time();
            payload_out.packet_type = QLCP_PT_CONFIG;

            const uint8_t sequence = atomic_fetch_add(&app_ctx->sequence, 1);

            const qlcp_config_packet config = {
                .config_data = (const uint8_t *) json_config_str,
                .config_data_len = JSON_CONFIG_LEN,
                .header = {
                           .sequence = sequence,
                           .timestamp_us = 0,
                           },
            };
            payload_out.payload_data.config = config;

            xQueueSend(app_ctx->network_ctx->tcp_send_queue_handle, (void *)&payload_out, 0);
            app_ctx->network_ctx->config_sent = true;
            ESP_LOGI(TAG, "Sent config to server");
        }
        // reset all controls to default state when watchdog timeout triggers
        if (esp_timer_get_time() - last_packet_time_us > WATCHDOG_RESET_TIMEOUT_US) {
            for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
                control_set_default(&app_ctx->controls[i]);
            }
            ESP_LOGW(TAG, "Software watchdog triggered, reset to default state");
            last_packet_time_us = esp_timer_get_time();
        }
        // check for incoming packets from the tcp recv queue
        if (xQueueReceive(app_ctx->network_ctx->tcp_recv_queue_handle, &payload_in, pdMS_TO_TICKS(100)) == pdTRUE) {

            last_packet_time_us = esp_timer_get_time();

            switch (payload_in.packet_type) {
            case QLCP_PT_ESTOP:
                estop_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_TIMESYNC_RESP: 
                timesync_resp_handler(app_ctx, &payload_in.payload_data.timesync_resp, &payload_out);
                break;
            case QLCP_PT_CONTROL:
                control_handler(app_ctx, &payload_in.payload_data.control, &payload_out);
                break;
            case QLCP_PT_STATUS_REQUEST:
                status_request_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_STREAM_START:
                stream_start_handler(app_ctx, &payload_in.payload_data.stream_start, &payload_out);
                break;
            case QLCP_PT_STREAM_STOP:
                stream_stop_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_GET_SINGLE:
                get_single_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_HEARTBEAT:
                heartbeat_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_ACK:
            case QLCP_PT_NACK:
                continue;
            default:
                ESP_LOGE(TAG, "Invalid client packet type recieved: %d", payload_in.packet_type);
                payload_out.packet_type = QLCP_PT_NACK;
                payload_out.payload_data.nack = make_nack_packet(payload_in.payload_data.header_only.header.sequence, payload_in.packet_type, QLCP_ERR_UNKNOWN_TYPE, app_ctx);
                break;
            }
            // send the packet out to the tcp send queue
            xQueueSend(app_ctx->network_ctx->tcp_send_queue_handle, (void *)&payload_out, MESSAGE_QUEUE_TIMEOUT);
        }
    }
}