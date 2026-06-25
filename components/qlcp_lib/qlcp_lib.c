#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "qlcp_lib.h"

#define QLCP_PROTOCOL_VERSION 2

// Internal header struct
typedef struct {
    uint8_t version;
    uint8_t packet_type;
    uint8_t sequence;
    uint16_t packet_length;
    uint32_t timestamp;
} qlcp_header_internal;

// Encode a header_data struct into the buffer
static qlcp_lib_ret s_pack_header(uint8_t buffer[], size_t buffer_len, const qlcp_header_internal *header_data) {
    if (buffer == NULL || header_data == NULL) {
        return QLCP_NULL_PTR;
    }
    if (buffer_len < QLCP_HEADER_SIZE) {
        return QLCP_NO_MEM;
    }

    buffer[0] = header_data->version;
    buffer[1] = header_data->packet_type;
    buffer[2] = header_data->sequence;

    buffer[3] = (uint8_t)(header_data->packet_length >> 8);
    buffer[4] = (uint8_t)header_data->packet_length;

    buffer[5] = (uint8_t)(header_data->timestamp >> 24);
    buffer[6] = (uint8_t)(header_data->timestamp >> 16);
    buffer[7] = (uint8_t)(header_data->timestamp >> 8);
    buffer[8] = (uint8_t)header_data->timestamp;

    return QLCP_OK;
}

// Take a buffer which contains the encoded header, and fill out the header_data struct
static qlcp_lib_ret s_unpack_header(qlcp_header_internal *header_data, const uint8_t buffer[], size_t buffer_len) {
    if (buffer == NULL || header_data == NULL) {
        return QLCP_NULL_PTR;
    }
    if (buffer_len < QLCP_HEADER_SIZE) {
        return QLCP_NO_MEM;
    }
    if (buffer[0] != QLCP_PROTOCOL_VERSION) {
        return QLCP_VERSION_MISMATCH;
    }

    header_data->version = buffer[0];
    header_data->packet_type = buffer[1];
    header_data->sequence = buffer[2];

    header_data->packet_length = ((uint16_t)buffer[3] << 8) | ((uint16_t)buffer[4]);

    header_data->timestamp = ((uint32_t)buffer[5] << 24) | ((uint32_t)buffer[6] << 16) | ((uint32_t)buffer[7] << 8) |
                             ((uint32_t)buffer[8]);

    return QLCP_OK;
}

static inline bool s_is_packet_header_only(qlcp_packet_type packet_type) {
    switch (packet_type) {
    case QLCP_PT_ESTOP:
    case QLCP_PT_DISCOVERY:
    case QLCP_PT_TIMESYNC:
    case QLCP_PT_STREAM_STOP:
    case QLCP_PT_GET_SINGLE:
    case QLCP_PT_HEARTBEAT:
    case QLCP_PT_STATUS_REQUEST:
        return true;
    default:
        return false;
    }
}

qlcp_lib_ret qlcp_encode_header_only(uint8_t buffer[], size_t *buffer_len, qlcp_packet_type packet_type, const qlcp_header_only_packet *header_only) {
    if (buffer == NULL || buffer_len == NULL || header_only == NULL) {
        return QLCP_NULL_PTR;
    }
    if (!s_is_packet_header_only(packet_type)) {
        return QLCP_INVALID_PACKET_TYPE;
    }
    if (*buffer_len < QLCP_HEADER_SIZE) {
        return QLCP_NO_MEM;
    }
    *buffer_len = QLCP_HEADER_SIZE;

    const qlcp_header_internal header_data = {
        .version = QLCP_PROTOCOL_VERSION,
        .packet_type = packet_type,
        .sequence = header_only->sequence,
        .packet_length = QLCP_HEADER_SIZE,
        .timestamp = header_only->timestamp,
    };

    qlcp_lib_ret ret = s_pack_header(buffer, *buffer_len, &header_data);
    if (ret != QLCP_OK) {
        return ret;
    }
    return QLCP_OK;
}

qlcp_lib_ret qlcp_encode_ack(uint8_t buffer[], size_t *buffer_len, const qlcp_ack_packet *ack) {
    if (buffer == NULL || buffer_len == NULL || ack == NULL) {
        return QLCP_NULL_PTR;
    }
    if (*buffer_len < QLCP_ACK_PACKET_SIZE) {
        return QLCP_NO_MEM;
    }
    *buffer_len = QLCP_ACK_PACKET_SIZE;

    const qlcp_header_internal header_data = {
        .version = QLCP_PROTOCOL_VERSION,
        .packet_type = QLCP_PT_ACK,
        .sequence = ack->header.sequence,
        .packet_length = QLCP_ACK_PACKET_SIZE,
        .timestamp = ack->header.timestamp,
    };

    qlcp_lib_ret ret = s_pack_header(buffer, *buffer_len, &header_data);
    if (ret != QLCP_OK) {
        return ret;
    }

    buffer[QLCP_HEADER_SIZE + 0] = ack->ack_packet_type;
    buffer[QLCP_HEADER_SIZE + 1] = ack->ack_sequence;
    buffer[QLCP_HEADER_SIZE + 2] = QLCP_ERR_NONE; // ack always has no error code

    return QLCP_OK;
}

qlcp_lib_ret qlcp_encode_nack(uint8_t buffer[], size_t *buffer_len, const qlcp_nack_packet *nack) {
    if (buffer == NULL || buffer_len == NULL || nack == NULL) {
        return QLCP_NULL_PTR;
    }
    if (*buffer_len < QLCP_NACK_PACKET_SIZE) {
        return QLCP_NO_MEM;
    }
    *buffer_len = QLCP_NACK_PACKET_SIZE;

    const qlcp_header_internal header_data = {
        .version = QLCP_PROTOCOL_VERSION,
        .packet_type = QLCP_PT_NACK,
        .sequence = nack->header.sequence,
        .packet_length = QLCP_NACK_PACKET_SIZE,
        .timestamp = nack->header.timestamp,
    };

    qlcp_lib_ret ret = s_pack_header(buffer, *buffer_len, &header_data);
    if (ret != QLCP_OK) {
        return ret;
    }

    buffer[QLCP_HEADER_SIZE + 0] = nack->nack_packet_type;
    buffer[QLCP_HEADER_SIZE + 1] = nack->nack_sequence;
    buffer[QLCP_HEADER_SIZE + 2] = nack->nack_error_code;

    return QLCP_OK;
}

qlcp_lib_ret qlcp_encode_stream_start(uint8_t buffer[], size_t *buffer_len, const qlcp_stream_start_packet *stream_start) {
    if (buffer == NULL || buffer_len == NULL || stream_start == NULL) {
        return QLCP_NULL_PTR;
    }
    if (*buffer_len < QLCP_STREAM_START_PACKET_SIZE) {
        return QLCP_NO_MEM;
    }
    *buffer_len = QLCP_STREAM_START_PACKET_SIZE;

    const qlcp_header_internal header_data = {
        .version = QLCP_PROTOCOL_VERSION,
        .packet_type = QLCP_PT_STREAM_START,
        .sequence = stream_start->header.sequence,
        .packet_length = QLCP_STREAM_START_PACKET_SIZE,
        .timestamp = stream_start->header.timestamp,
    };

    qlcp_lib_ret ret = s_pack_header(buffer, *buffer_len, &header_data);
    if (ret != QLCP_OK) {
        return ret;
    }

    buffer[QLCP_HEADER_SIZE + 0] = (uint8_t)(stream_start->stream_frequency >> 8);
    buffer[QLCP_HEADER_SIZE + 1] = (uint8_t)stream_start->stream_frequency;

    return QLCP_OK;
}

qlcp_lib_ret qlcp_encode_control(uint8_t buffer[], size_t *buffer_len, const qlcp_control_packet *control) {
    if (buffer == NULL) {
        return QLCP_NULL_PTR;
    }
    if (*buffer_len < QLCP_CONTROL_PACKET_SIZE) {
        return QLCP_NO_MEM;
    }
    *buffer_len = QLCP_CONTROL_PACKET_SIZE;

    const qlcp_header_internal header_data = {
        .version = QLCP_PROTOCOL_VERSION,
        .packet_type = QLCP_PT_CONTROL,
        .sequence = control->header.sequence,
        .packet_length = QLCP_CONTROL_PACKET_SIZE,
        .timestamp = control->header.timestamp,
    };

    qlcp_lib_ret ret = s_pack_header(buffer, *buffer_len, &header_data);
    if (ret != QLCP_OK) {
        return ret;
    }

    buffer[QLCP_HEADER_SIZE + 0] = control->command_id;
    buffer[QLCP_HEADER_SIZE + 1] = control->command_state;

    return QLCP_OK;
}

qlcp_lib_ret qlcp_encode_status(uint8_t buffer[], size_t *buffer_len, const qlcp_status_packet *status) {
    if (buffer == NULL || buffer_len == NULL || status == NULL || status->control_data == NULL) {
        return QLCP_NULL_PTR;
    }
    if (*buffer_len < QLCP_STATUS_PACKET_SIZE(status->control_count)) {
        return QLCP_NO_MEM;
    }
    *buffer_len = QLCP_STATUS_PACKET_SIZE(status->control_count);

    const qlcp_header_internal header_data = {
        .version = QLCP_PROTOCOL_VERSION,
        .packet_type = QLCP_PT_STATUS,
        .sequence = status->header.sequence,
        .packet_length = QLCP_STATUS_PACKET_SIZE(status->control_count),
        .timestamp = status->header.timestamp,
    };

    qlcp_lib_ret ret = s_pack_header(buffer, *buffer_len, &header_data);
    if (ret != QLCP_OK) {
        return ret;
    }

    buffer[QLCP_HEADER_SIZE + 0] = status->device_status;
    buffer[QLCP_HEADER_SIZE + 1] = status->control_count;

    for (size_t i = 0; i < status->control_count; i++) {
        const size_t offset = QLCP_STATUS_PACKET_SIZE(i);
        buffer[offset + 0] = status->control_data[i].control_id;
        buffer[offset + 1] = status->control_data[i].control_state;
    }
    return QLCP_OK;
}

qlcp_lib_ret qlcp_encode_data(uint8_t buffer[], size_t *buffer_len, const qlcp_data_packet *data) {
    if (buffer == NULL || buffer_len == NULL || data == NULL || data->sensor_data == NULL) {
        return QLCP_NULL_PTR;
    }
    if (*buffer_len < QLCP_DATA_PACKET_SIZE(data->sensor_count)) {
        return QLCP_NO_MEM;
    }
    *buffer_len = QLCP_DATA_PACKET_SIZE(data->sensor_count);

    const qlcp_header_internal header_data = {
        .version = QLCP_PROTOCOL_VERSION,
        .packet_type = QLCP_PT_DATA,
        .sequence = data->header.sequence,
        .packet_length = QLCP_DATA_PACKET_SIZE(data->sensor_count),
        .timestamp = data->header.timestamp,
    };

    qlcp_lib_ret ret = s_pack_header(buffer, *buffer_len, &header_data);
    if (ret != QLCP_OK) {
        return ret;
    }

    buffer[QLCP_HEADER_SIZE + 0] = data->sensor_count;

    for (size_t i = 0; i < data->sensor_count; i++) {
        const size_t offset = QLCP_DATA_PACKET_SIZE(i);
        buffer[offset + 0] = data->sensor_data[i].sensor_id;
        buffer[offset + 1] = data->sensor_data[i].unit;
        // Copy the exact bytes from a float
        uint32_t value_bytes;
        memcpy(&value_bytes, &data->sensor_data[i].value, sizeof(uint32_t));
        buffer[offset + 2] = (uint8_t)(value_bytes >> 24);
        buffer[offset + 3] = (uint8_t)(value_bytes >> 16);
        buffer[offset + 4] = (uint8_t)(value_bytes >> 8);
        buffer[offset + 5] = (uint8_t)(value_bytes);
    }
    return QLCP_OK;
}

qlcp_lib_ret qlcp_encode_config(uint8_t buffer[], size_t *buffer_len, const qlcp_config_packet *config) {
    if (buffer == NULL || buffer_len == NULL || config == NULL || config->config_data == NULL) {
        return QLCP_NULL_PTR;
    }
    uint32_t packet_data_len = config->config_data_len;
    if (config->config_data_len > 0 && config->config_data[config->config_data_len - 1] == '\0') {
        packet_data_len--; // Remove null terminator from config_data
    }
    if (*buffer_len < QLCP_CONFIG_PACKET_SIZE(packet_data_len)) {
        return QLCP_NO_MEM;
    }
    *buffer_len = QLCP_CONFIG_PACKET_SIZE(packet_data_len);

    const qlcp_header_internal header_data = {
        .version = QLCP_PROTOCOL_VERSION,
        .packet_type = QLCP_PT_CONFIG,
        .sequence = config->header.sequence,
        .packet_length = QLCP_CONFIG_PACKET_SIZE(packet_data_len),
        .timestamp = config->header.timestamp,
    };

    qlcp_lib_ret ret = s_pack_header(buffer, *buffer_len, &header_data);
    if (ret != QLCP_OK) {
        return ret;
    }

    buffer[QLCP_HEADER_SIZE + 0] = (uint8_t)(packet_data_len >> 24);
    buffer[QLCP_HEADER_SIZE + 1] = (uint8_t)(packet_data_len >> 16);
    buffer[QLCP_HEADER_SIZE + 2] = (uint8_t)(packet_data_len >> 8);
    buffer[QLCP_HEADER_SIZE + 3] = (uint8_t)packet_data_len;

    memcpy(buffer + QLCP_CONFIG_PACKET_SIZE(0), config->config_data, packet_data_len);

    return QLCP_OK;
}

qlcp_lib_ret qlcp_get_packet_len(uint16_t *data_len, const uint8_t buffer[], size_t buffer_len) {
    if (buffer == NULL || data_len == NULL) {
        return QLCP_NULL_PTR;
    }
    if (buffer_len < QLCP_HEADER_SIZE) {
        return QLCP_NO_MEM;
    }
    qlcp_header_internal header_data = {0};

    qlcp_lib_ret ret = s_unpack_header(&header_data, buffer, buffer_len);
    if (ret != QLCP_OK) {
        return ret;
    }

    *data_len = header_data.packet_length;
    return QLCP_OK;
}

qlcp_lib_ret qlcp_decode_client_to_server(qlcp_server_payload *payload, qlcp_server_payload_buffers *payload_buffers, const uint8_t buffer[], size_t buffer_len) {
    if (buffer == NULL || payload == NULL || payload_buffers == NULL) {
        return QLCP_NULL_PTR;
    }
    if (payload_buffers->control_data == NULL || payload_buffers->sensor_data == NULL || payload_buffers->config_data == NULL) {
        return QLCP_NULL_PTR;
    }
    if (buffer_len < QLCP_HEADER_SIZE) {
        return QLCP_NO_MEM;
    }
    // First decode the header to get the packet type
    qlcp_header_internal header_data = {0};

    qlcp_lib_ret ret = s_unpack_header(&header_data, buffer, buffer_len);
    if (ret != QLCP_OK) {
        return ret;
    }
    if (buffer_len < header_data.packet_length) {
        return QLCP_NO_MEM;
    }

    // Tag the packet type
    payload->packet_type = header_data.packet_type;

    switch (header_data.packet_type) {
    case QLCP_PT_ACK:
        if (header_data.packet_length != QLCP_ACK_PACKET_SIZE) {
            return QLCP_LEN_MISMATCH;
        }
        if (buffer[QLCP_HEADER_SIZE + 2] != QLCP_ERR_NONE) {
            return QLCP_INVALID_PACKET_TYPE;
        }
        payload->payload_data.ack.header.sequence = header_data.sequence;
        payload->payload_data.ack.header.timestamp = header_data.timestamp;

        payload->payload_data.ack.ack_packet_type = buffer[QLCP_HEADER_SIZE + 0];
        payload->payload_data.ack.ack_sequence = buffer[QLCP_HEADER_SIZE + 1];
        break;
    case QLCP_PT_NACK:
        if (header_data.packet_length != QLCP_NACK_PACKET_SIZE) {
            return QLCP_LEN_MISMATCH;
        }
        payload->payload_data.nack.header.sequence = header_data.sequence;
        payload->payload_data.nack.header.timestamp = header_data.timestamp;

        payload->payload_data.nack.nack_packet_type = buffer[QLCP_HEADER_SIZE + 0];
        payload->payload_data.nack.nack_sequence = buffer[QLCP_HEADER_SIZE + 1];
        payload->payload_data.nack.nack_error_code = buffer[QLCP_HEADER_SIZE + 2];
        break;
    case QLCP_PT_STATUS:
        // Check that there is enough memory in buffers
        uint8_t control_count = buffer[QLCP_HEADER_SIZE + 1];
        if (payload_buffers->control_data_len < control_count) {
            return QLCP_NO_MEM;
        }
        if (header_data.packet_length != QLCP_STATUS_PACKET_SIZE(control_count)) {
            return QLCP_LEN_MISMATCH;
        }
        payload->payload_data.status.header.sequence = header_data.sequence;
        payload->payload_data.status.header.timestamp = header_data.timestamp;

        payload->payload_data.status.device_status = buffer[QLCP_HEADER_SIZE + 0];
        payload->payload_data.status.control_count = buffer[QLCP_HEADER_SIZE + 1];

        payload->payload_data.status.control_data = payload_buffers->control_data;
        for (size_t i = 0; i < control_count; i++) {
            size_t offset = QLCP_STATUS_PACKET_SIZE(i);
            payload_buffers->control_data[i].control_id = buffer[offset + 0];
            payload_buffers->control_data[i].control_state = buffer[offset + 1];
        }
        break;
    case QLCP_PT_DATA:
        // Check that there is enough memory in buffers
        uint8_t sensor_count = buffer[QLCP_HEADER_SIZE + 0];
        if (payload_buffers->sensor_data_len < sensor_count) {
            return QLCP_NO_MEM;
        }
        if (header_data.packet_length != QLCP_DATA_PACKET_SIZE(sensor_count)) {
            return QLCP_LEN_MISMATCH;
        }
        payload->payload_data.data.header.sequence = header_data.sequence;
        payload->payload_data.data.header.timestamp = header_data.timestamp;

        payload->payload_data.data.sensor_count = sensor_count;

        payload->payload_data.data.sensor_data = payload_buffers->sensor_data;
        for (size_t i = 0; i < sensor_count; i++) {
            size_t offset = QLCP_DATA_PACKET_SIZE(i);
            payload_buffers->sensor_data[i].sensor_id = buffer[offset + 0];
            payload_buffers->sensor_data[i].unit = buffer[offset + 1];
            // Bytes to float
            uint32_t value_bytes = ((uint32_t)buffer[offset + 2] << 24) | ((uint32_t)buffer[offset + 3] << 16) |
                                   ((uint32_t)buffer[offset + 4] << 8) | ((uint32_t)buffer[offset + 5]);
            memcpy(&payload_buffers->sensor_data[i].value, &value_bytes, sizeof(uint32_t));
        }
        break;
    case QLCP_PT_CONFIG:
        // Check that there is enough memory in buffers
        uint32_t data_len = ((uint32_t)buffer[QLCP_HEADER_SIZE + 0] << 24) | ((uint32_t)buffer[QLCP_HEADER_SIZE + 1] << 16) |
                            ((uint32_t)buffer[QLCP_HEADER_SIZE + 2] << 8) | (uint32_t)buffer[QLCP_HEADER_SIZE + 3];
        if (payload_buffers->config_data_len < data_len) {
            return QLCP_NO_MEM;
        }
        if (header_data.packet_length != QLCP_CONFIG_PACKET_SIZE(data_len)) {
            return QLCP_LEN_MISMATCH;
        }
        payload->payload_data.config.header.sequence = header_data.sequence;
        payload->payload_data.config.header.timestamp = header_data.timestamp;

        payload->payload_data.config.config_data_len = data_len;

        payload->payload_data.config.config_data = payload_buffers->config_data;
        memcpy(payload_buffers->config_data, buffer + QLCP_CONFIG_PACKET_SIZE(0), data_len);
        break;
    default:
        return QLCP_INVALID_PACKET_TYPE;
    }

    return QLCP_OK;
}

qlcp_lib_ret qlcp_decode_server_to_client(qlcp_client_payload *payload, const uint8_t buffer[], size_t buffer_len) {
    if (buffer == NULL || payload == NULL) {
        return QLCP_NULL_PTR;
    }
    if (buffer_len < QLCP_HEADER_SIZE) {
        return QLCP_NO_MEM;
    }
    // First decode the header to get the packet type
    qlcp_header_internal header_data = {0};

    qlcp_lib_ret ret = s_unpack_header(&header_data, buffer, buffer_len);
    if (ret != QLCP_OK) {
        return ret;
    }
    if (buffer_len < header_data.packet_length) {
        return QLCP_NO_MEM;
    }

    // Tag the packet type
    payload->packet_type = header_data.packet_type;

    switch (header_data.packet_type) {
    case QLCP_PT_ESTOP:
    case QLCP_PT_DISCOVERY:
    case QLCP_PT_TIMESYNC:
    case QLCP_PT_STREAM_STOP:
    case QLCP_PT_GET_SINGLE:
    case QLCP_PT_HEARTBEAT:
    case QLCP_PT_STATUS_REQUEST:
        if (header_data.packet_length != QLCP_HEADER_SIZE) {
            return QLCP_LEN_MISMATCH;
        }
        payload->payload_data.header_only.sequence = header_data.sequence;
        payload->payload_data.header_only.timestamp = header_data.timestamp;
        break;
    case QLCP_PT_ACK:
        if (header_data.packet_length != QLCP_ACK_PACKET_SIZE) {
            return QLCP_LEN_MISMATCH;
        }
        if (buffer[QLCP_HEADER_SIZE + 2] != QLCP_ERR_NONE) {
            return QLCP_INVALID_PACKET_TYPE;
        }
        payload->payload_data.ack.header.sequence = header_data.sequence;
        payload->payload_data.ack.header.timestamp = header_data.timestamp;

        payload->payload_data.ack.ack_packet_type = buffer[QLCP_HEADER_SIZE + 0];
        payload->payload_data.ack.ack_sequence = buffer[QLCP_HEADER_SIZE + 1];
        break;
    case QLCP_PT_NACK:
        if (header_data.packet_length != QLCP_NACK_PACKET_SIZE) {
            return QLCP_LEN_MISMATCH;
        }
        payload->payload_data.nack.header.sequence = header_data.sequence;
        payload->payload_data.nack.header.timestamp = header_data.timestamp;

        payload->payload_data.nack.nack_packet_type = buffer[QLCP_HEADER_SIZE + 0];
        payload->payload_data.nack.nack_sequence = buffer[QLCP_HEADER_SIZE + 1];
        payload->payload_data.nack.nack_error_code = buffer[QLCP_HEADER_SIZE + 2];
        break;
    case QLCP_PT_STREAM_START:
        if (header_data.packet_length != QLCP_STREAM_START_PACKET_SIZE) {
            return QLCP_LEN_MISMATCH;
        }
        payload->payload_data.stream_start.header.sequence = header_data.sequence;
        payload->payload_data.stream_start.header.timestamp = header_data.timestamp;

        payload->payload_data.stream_start.stream_frequency = ((uint16_t)buffer[QLCP_HEADER_SIZE + 0] << 8) |
                                                              (uint16_t)buffer[QLCP_HEADER_SIZE + 1];
        break;
    case QLCP_PT_CONTROL:
        if (header_data.packet_length != QLCP_CONTROL_PACKET_SIZE) {
            return QLCP_LEN_MISMATCH;
        }
        payload->payload_data.control.header.sequence = header_data.sequence;
        payload->payload_data.control.header.timestamp = header_data.timestamp;

        payload->payload_data.control.command_id = buffer[QLCP_HEADER_SIZE + 0];
        payload->payload_data.control.command_state = buffer[QLCP_HEADER_SIZE + 1];
        break;
    default:
        return QLCP_INVALID_PACKET_TYPE;
    }

    return QLCP_OK;
}
