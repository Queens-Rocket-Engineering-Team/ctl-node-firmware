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
#define WATCHDOG_RESET_TIMEOUT_US ((WATCHDOG_RESET_TIMEOUT_MIN) * 60 * 1000000ULL)

#define TIMESYNC_REQ_PERIOD_S 60
#define TIMESYNC_REQ_PERIOD_US ((TIMESYNC_REQ_PERIOD_S) * 1000000ULL)

#define STATUS_RING_BUFFER_LEN ((TCP_SEND_QUEUE_LEN) + 2)
// +2 to account for packet taken out of queue by
// udp_send + extra packet to write to when queue is full

static const char *TAG = "PACKET HANDLER";

// timesync req

static void s_timesync_req_callback(void *ctx) {
    app_ctx_t *app_ctx = (app_ctx_t *) ctx;

    const int64_t current_ts_offset = atomic_load(&app_ctx->ts_offset);
    const int64_t timestamp_us = esp_timer_get_time() - current_ts_offset;
    const uint8_t sequence = atomic_fetch_add(&app_ctx->sequence, 1);

    const qlcp_header_only_packet timesync_req = {
        .packet_type = QLCP_PT_TIMESYNC_REQ,
        .header = {
                   .sequence = sequence,
                   .timestamp_us = timestamp_us,
                   },
    };

    const qlcp_server_payload payload_out = {
        .packet_type = QLCP_PT_TIMESYNC_REQ,
        .payload_data.header_only = timesync_req,
    };

    xQueueSend(app_ctx->network_ctx->tcp_send_queue_handle, (void *)&payload_out, 0);
}

// helpers

static qlcp_ack_packet s_make_ack_packet(uint8_t ack_sequence, qlcp_packet_type ack_type, app_ctx_t *app_ctx) {
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

static qlcp_nack_packet s_make_nack_packet(uint8_t nack_sequence, qlcp_packet_type nack_type, qlcp_err_code nack_err, app_ctx_t *app_ctx) {
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

static qlcp_status_packet s_make_status_packet(uint8_t ack_sequence, qlcp_packet_type ack_type, app_ctx_t *app_ctx) {
    static uint8_t ring_buffer_idx = 0;
    static qlcp_status_data status_data_ring_buffer[STATUS_RING_BUFFER_LEN][CONFIG_NUM_CONTROLS] = {0};

    qlcp_status_data *status_data = status_data_ring_buffer[ring_buffer_idx];

    ring_buffer_idx = (ring_buffer_idx + 1) % STATUS_RING_BUFFER_LEN;

    memset(status_data, 0, sizeof(qlcp_control_data) * CONFIG_NUM_CONTROLS);

    // right now this only handles bool/v_uint32 controls
    for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
        status_data[i].id = i;
        status_data[i].status = QLCP_CONTROL_STATUS_CONFIRMED;

        switch (app_ctx->controls[i].control_type) {
        case CONTROL_TYPE_H_BOOL:
            {
                status_data[i].type = QLCP_CONTROL_BOOL;

                const h_bool_control_state_t control_internal_state = h_bool_control_get_state(&app_ctx->controls[i].control.h_bool);
                switch (control_internal_state) {
                case BOOL_CONTROL_OPEN:
                    status_data[i].state.control_bool = QLCP_CS_OPEN;
                    break;
                case BOOL_CONTROL_CLOSED:
                    status_data[i].state.control_bool = QLCP_CS_CLOSED;
                    break;
                case BOOL_CONTROL_UNKNOWN:
                    status_data[i].status = QLCP_CONTROL_STATUS_ERROR;
                    break;
                };
            }
            break;
        case CONTROL_TYPE_V_UINT32:
            status_data[i].type = QLCP_CONTROL_UINT32;
            status_data[i].state.control_uint32 = v_uint32_control_get_state(&app_ctx->controls[i].control.v_uint32);
            break;
        default:
            continue;
        }
    }

    const int64_t current_ts_offset = atomic_load(&app_ctx->ts_offset);
    const int64_t timestamp_us = esp_timer_get_time() - current_ts_offset;
    const uint8_t sequence = atomic_fetch_add(&app_ctx->sequence, 1);

    const qlcp_status_packet status = {
        .control_data = status_data,
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

static void s_estop_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *estop_packet, qlcp_server_payload *payload_out) {
    for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
        app_ctx->controls[i].control.base.set_default(&app_ctx->controls[i].control.base);
    }
    ESP_LOGW(TAG, "Received ESTOP, reset to default state");

    payload_out->packet_type = QLCP_PT_STATUS;
    payload_out->payload_data.status = s_make_status_packet(estop_packet->header.sequence, QLCP_PT_ESTOP, app_ctx);
}

static void s_timesync_resp_handler(app_ctx_t *app_ctx, qlcp_timesync_resp_packet *timesync_resp_packet, qlcp_server_payload *payload_out) {
    const int64_t t1 = (int64_t) timesync_resp_packet->t1_echo_us;
    const int64_t t2 = (int64_t) timesync_resp_packet->t2_us;
    const int64_t t3 = (int64_t) timesync_resp_packet->header.timestamp_us;
    const int64_t t4 = (int64_t) esp_timer_get_time();
    const int64_t new_ts_offset = ((t1 - t2) + (t4 - t3)) / 2;
    atomic_store(&app_ctx->ts_offset, new_ts_offset);

    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = s_make_ack_packet(timesync_resp_packet->header.sequence, QLCP_PT_TIMESYNC_RESP, app_ctx);
}

static void s_control_handler(app_ctx_t *app_ctx, qlcp_control_packet *control_packet, qlcp_server_payload *payload_out) {
    const uint8_t i = control_packet->control_data.id;

    esp_err_t err = ESP_FAIL;
    qlcp_err_code nack_err = QLCP_ERR_HARDWARE_FAULT;

    // ensure the control index is valid
    if (i < CONFIG_NUM_CONTROLS) {
        switch (control_packet->control_data.type) {
            case QLCP_CONTROL_BOOL:
                if (app_ctx->controls[i].control_type != CONTROL_TYPE_H_BOOL) {
                    err = ESP_ERR_INVALID_ARG;
                    nack_err = QLCP_ERR_INVALID_PARAM;
                    break;
                }

                if (control_packet->control_data.state.control_bool == QLCP_CS_OPEN) {
                    err = h_bool_control_open(&app_ctx->controls[i].control.h_bool);
                } else if (control_packet->control_data.state.control_bool == QLCP_CS_CLOSED) {
                    err = h_bool_control_close(&app_ctx->controls[i].control.h_bool);
                } else {
                    nack_err = QLCP_ERR_INVALID_PARAM;
                }
                break;
            case QLCP_CONTROL_UINT32:
                if (app_ctx->controls[i].control_type != CONTROL_TYPE_V_UINT32) {
                    err = ESP_ERR_INVALID_ARG;
                    nack_err = QLCP_ERR_INVALID_PARAM;
                    break;
                }

                err = v_uint32_control_set(&app_ctx->controls[i].control.v_uint32, control_packet->control_data.state.control_uint32);
                break;
            // currently unsupported
            case QLCP_CONTROL_INT32:
            case QLCP_CONTROL_FLOAT32:
            default:
                err = ESP_ERR_INVALID_ARG;
                nack_err = QLCP_ERR_INVALID_PARAM;
                break;
        }
    }

    if (err == ESP_OK) {
        payload_out->packet_type = QLCP_PT_STATUS;
        payload_out->payload_data.status = s_make_status_packet(control_packet->header.sequence, QLCP_PT_CONTROL, app_ctx);
    } else {
        ESP_LOGE(TAG, "%s", esp_err_to_name(err));
        payload_out->packet_type = QLCP_PT_NACK;
        payload_out->payload_data.nack = s_make_nack_packet(control_packet->header.sequence, QLCP_PT_CONTROL, nack_err, app_ctx);
    }
}

static void s_status_request_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *status_request_packet, qlcp_server_payload *payload_out) {
    payload_out->packet_type = QLCP_PT_STATUS;
    payload_out->payload_data.status = s_make_status_packet(status_request_packet->header.sequence, QLCP_PT_STATUS_REQUEST, app_ctx);
}

static void s_stream_start_handler(app_ctx_t *app_ctx, qlcp_stream_start_packet *stream_start_packet, qlcp_server_payload *payload_out) {
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
    payload_out->payload_data.ack = s_make_ack_packet(stream_start_packet->header.sequence, QLCP_PT_STREAM_START, app_ctx);
}

static void s_stream_stop_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *stream_stop_packet, qlcp_server_payload *payload_out) {
    xEventGroupClearBits(app_ctx->sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
    ESP_LOGI(TAG, "Sensor stream stopped");

    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = s_make_ack_packet(stream_stop_packet->header.sequence, QLCP_PT_STREAM_STOP, app_ctx);
}

static void s_get_single_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *get_single_packet, qlcp_server_payload *payload_out) {
    xEventGroupSetBits(app_ctx->sensor_stream_event_group_handle, SENSORS_SINGLE_READING_BIT);
    ESP_LOGI(TAG, "Sensors single reading");

    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = s_make_ack_packet(get_single_packet->header.sequence, QLCP_PT_GET_SINGLE, app_ctx);
}

static void s_heartbeat_handler(app_ctx_t *app_ctx, qlcp_header_only_packet *heartbeat_packet, qlcp_server_payload *payload_out) {
    payload_out->packet_type = QLCP_PT_ACK;
    payload_out->payload_data.ack = s_make_ack_packet(heartbeat_packet->header.sequence, QLCP_PT_HEARTBEAT, app_ctx);
}

// handler loop
void packet_handler(void *pvParams) {
    app_ctx_t *app_ctx = (app_ctx_t *) pvParams;
    
    // reset watchdog timer
    uint64_t last_packet_time_us = esp_timer_get_time();
    // processing for incoming/outgoing packets
    qlcp_client_payload payload_in = {0};
    qlcp_server_payload payload_out = {0};

    // create the timer for timesync reqs
    const esp_timer_create_args_t timesync_req_timer_args = {
        .callback = &s_timesync_req_callback,
        .arg = app_ctx,
        .name = "timesync timer",
    };
    esp_timer_handle_t timesync_req_timer = {0};
    esp_timer_create(&timesync_req_timer_args, &timesync_req_timer);

    while (1) {

        EventBits_t wifi_bits = xEventGroupGetBits(app_ctx->network_ctx->wifi_event_group_handle);
        EventBits_t stream_bits = xEventGroupGetBits(app_ctx->sensor_stream_event_group_handle);

        if (!(wifi_bits & SERVER_CONNECTED_BIT)) {
            // reset config sent bool
            app_ctx->config_sent = false;
            // disable data stream if disconnected
            if (stream_bits & SENSOR_STREAM_ENABLE_BIT) {
                xEventGroupClearBits(app_ctx->sensor_stream_event_group_handle, SENSOR_STREAM_ENABLE_BIT);
                ESP_LOGI(TAG, "Sensor stream stopped");
            }
            // stop timesync req timer if disconnected
            if (esp_timer_is_active(timesync_req_timer)) {
                esp_timer_stop(timesync_req_timer);
            }
        }

        // send config on connection
        if (!app_ctx->config_sent && (wifi_bits & SERVER_CONNECTED_BIT)) {
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

            if (xQueueSend(app_ctx->network_ctx->tcp_send_queue_handle, (void *)&payload_out, 0) == pdTRUE) {
                app_ctx->config_sent = true;
                ESP_LOGI(TAG, "Sent config to server");
            } else {
                ESP_LOGW(TAG, "Failed to send config to TCP queue");
            }

        }
        // reset all controls to default state when watchdog timeout triggers
        if (esp_timer_get_time() - last_packet_time_us > WATCHDOG_RESET_TIMEOUT_US) {
            for (size_t i = 0; i < CONFIG_NUM_CONTROLS; i++) {
                app_ctx->controls[i].control.base.set_default(&app_ctx->controls[i].control.base);
            }
            ESP_LOGW(TAG, "Software watchdog triggered, reset to default state");
            last_packet_time_us = esp_timer_get_time();
            app_ctx->config_sent = false;
        }
        // check for incoming packets from the tcp recv queue
        if (xQueueReceive(app_ctx->network_ctx->tcp_recv_queue_handle, &payload_in, pdMS_TO_TICKS(100)) == pdTRUE) {

            memset(&payload_out, 0, sizeof(qlcp_server_payload));

            last_packet_time_us = esp_timer_get_time();

            switch (payload_in.packet_type) {
            case QLCP_PT_ESTOP:
                s_estop_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_TIMESYNC_RESP: 
                s_timesync_resp_handler(app_ctx, &payload_in.payload_data.timesync_resp, &payload_out);
                break;
            case QLCP_PT_CONTROL:
                s_control_handler(app_ctx, &payload_in.payload_data.control, &payload_out);
                break;
            case QLCP_PT_STATUS_REQUEST:
                s_status_request_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_STREAM_START:
                s_stream_start_handler(app_ctx, &payload_in.payload_data.stream_start, &payload_out);
                break;
            case QLCP_PT_STREAM_STOP:
                s_stream_stop_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_GET_SINGLE:
                s_get_single_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_HEARTBEAT:
                s_heartbeat_handler(app_ctx, &payload_in.payload_data.header_only, &payload_out);
                break;
            case QLCP_PT_ACK:
                if (payload_in.payload_data.ack.ack_packet_type == QLCP_PT_CONFIG && !esp_timer_is_active(timesync_req_timer)) {
                    // start timesync req loop on config ack
                    esp_timer_start_periodic(timesync_req_timer, TIMESYNC_REQ_PERIOD_US);
                }
                continue;
            case QLCP_PT_NACK:
                continue;
            default:
                ESP_LOGE(TAG, "Invalid client packet type recieved: %d", payload_in.packet_type);
                payload_out.packet_type = QLCP_PT_NACK;
                payload_out.payload_data.nack = s_make_nack_packet(payload_in.payload_data.header_only.header.sequence, payload_in.packet_type, QLCP_ERR_UNKNOWN_TYPE, app_ctx);
                break;
            }
            // send the packet out to the tcp send queue
            xQueueSend(app_ctx->network_ctx->tcp_send_queue_handle, (void *)&payload_out, MESSAGE_QUEUE_TIMEOUT);
        }
    }
}