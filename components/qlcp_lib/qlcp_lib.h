#ifndef QLCP_LIB_H
#define QLCP_LIB_H

#include <stdint.h>

//----------------------------------------------------------
// Enums
//----------------------------------------------------------

typedef enum {
    // Return error codes
    QLCP_OK,
    QLCP_NULL_PTR,
    QLCP_NO_MEM,
    QLCP_LEN_MISMATCH,
    QLCP_VERSION_MISMATCH,
    QLCP_INVALID_PACKET_TYPE,
} qlcp_lib_ret;

typedef enum {
    // Server -> Device
    QLCP_PT_ESTOP = 0x00,
    QLCP_PT_DISCOVERY = 0x01,
    QLCP_PT_TIMESYNC = 0x02,
    QLCP_PT_CONTROL = 0x03,
    QLCP_PT_STATUS_REQUEST = 0x04,
    QLCP_PT_STREAM_START = 0x05,
    QLCP_PT_STREAM_STOP = 0x06,
    QLCP_PT_GET_SINGLE = 0x07,
    QLCP_PT_HEARTBEAT = 0x08,
    // Device -> Server
    QLCP_PT_CONFIG = 0x10,
    QLCP_PT_DATA = 0x11,
    QLCP_PT_STATUS = 0x12,
    // All
    QLCP_PT_ACK = 0x13,
    QLCP_PT_NACK = 0x14,
} qlcp_packet_type;

typedef enum {
    // Device status
    QLCP_DS_INACTIVE = 0x00,
    QLCP_DS_ACTIVE = 0x01,
    QLCP_DS_ERROR = 0x02,
    QLCP_DS_CALIBRATING = 0x03,
} qlcp_device_status;

typedef enum {
    // Control state
    QLCP_CS_CLOSED = 0x00,
    QLCP_CS_OPEN = 0x01,
    QLCP_CS_ERROR = 0xFF,
} qlcp_control_state;

typedef enum {
    QLCP_UNIT_VOLTS = 0x00,
    QLCP_UNIT_AMPS = 0x01,
    QLCP_UNIT_CELSIUS = 0x02,
    QLCP_UNIT_FAHRENHEIT = 0x03,
    QLCP_UNIT_KELVIN = 0x04,
    QLCP_UNIT_PSI = 0x05,
    QLCP_UNIT_BAR = 0x06,
    QLCP_UNIT_PASCAL = 0x07,
    QLCP_UNIT_GRAMS = 0x08,
    QLCP_UNIT_KILOGRAMS = 0x09,
    QLCP_UNIT_POUNDS = 0x0A,
    QLCP_UNIT_NEWTONS = 0x0B,
    QLCP_UNIT_SECONDS = 0x0C,
    QLCP_UNIT_MILLISECONDS = 0x0D,
    QLCP_UNIT_HERTZ = 0x0E,
    QLCP_UNIT_OHMS = 0x0F,
    QLCP_UNIT_UNITLESS = 0xFF,
} qlcp_unit;

typedef enum {
    // Packet error codes
    QLCP_ERR_NONE = 0x00,
    QLCP_ERR_UNKNOWN_TYPE = 0x01,
    QLCP_ERR_INVALID_ID = 0x02,
    QLCP_ERR_HARDWARE_FAULT = 0x03,
    QLCP_ERR_BUSY = 0x04,
    QLCP_ERR_NOT_STREAMING = 0x05,
    QLCP_ERR_INVALID_PARAM = 0x06,
} qlcp_err_code;

//----------------------------------------------------------
// Packet sizes
//----------------------------------------------------------

#define QLCP_STREAM_START_DATA_SIZE 2
#define QLCP_CONTROL_DATA_SIZE 2
#define QLCP_ACK_DATA_SIZE 3
#define QLCP_NACK_DATA_SIZE 3

#define QLCP_CONTROL_STATUS_DATA_SIZE 2
#define QLCP_STATUS_DATA_SIZE(control_count) (2 + (QLCP_CONTROL_STATUS_DATA_SIZE * control_count))

#define QLCP_SENSOR_DATA_SIZE 6
#define QLCP_DATA_DATA_SIZE(sensor_count) (1 + (QLCP_SENSOR_DATA_SIZE * sensor_count))

#define QLCP_CONFIG_DATA_SIZE(config_len) (4 + config_len)

#define QLCP_HEADER_SIZE 9

#define QLCP_STREAM_START_PACKET_SIZE (QLCP_HEADER_SIZE + QLCP_STREAM_START_DATA_SIZE)
#define QLCP_CONTROL_PACKET_SIZE (QLCP_HEADER_SIZE + QLCP_CONTROL_DATA_SIZE)
#define QLCP_ACK_PACKET_SIZE (QLCP_HEADER_SIZE + QLCP_ACK_DATA_SIZE)
#define QLCP_NACK_PACKET_SIZE (QLCP_HEADER_SIZE + QLCP_NACK_DATA_SIZE)
#define QLCP_STATUS_PACKET_SIZE(control_count) (QLCP_HEADER_SIZE + QLCP_STATUS_DATA_SIZE(control_count))
#define QLCP_DATA_PACKET_SIZE(sensor_count) (QLCP_HEADER_SIZE + QLCP_DATA_DATA_SIZE(sensor_count))
#define QLCP_CONFIG_PACKET_SIZE(config_len) (QLCP_HEADER_SIZE + QLCP_CONFIG_DATA_SIZE(config_len))

//----------------------------------------------------------
// Packet info structs
//----------------------------------------------------------

// Generic header struct
typedef struct {
    uint8_t sequence;
    uint32_t timestamp;
} qlcp_header;

// Struct for variable-length control data data in status packet
typedef struct {
    uint8_t control_id;
    uint8_t control_state;
} qlcp_control_data;

// Struct for variable-length sensor data in data packet
typedef struct {
    uint8_t sensor_id;
    uint8_t unit;
    float value;
} qlcp_sensor_data;

// Packet structs

typedef qlcp_header qlcp_header_only_packet;

typedef struct {
    qlcp_header header;
    uint16_t stream_frequency;
} qlcp_stream_start_packet;

typedef struct {
    qlcp_header header;
    uint8_t command_id;
    uint8_t command_state;
} qlcp_control_packet;

typedef struct {
    qlcp_header header;
    uint8_t ack_packet_type;
    uint8_t ack_sequence;
} qlcp_ack_packet;

typedef struct {
    qlcp_header header;
    uint8_t nack_packet_type;
    uint8_t nack_sequence;
    uint8_t nack_error_code;
} qlcp_nack_packet;

// Variable length packets

typedef struct {
    qlcp_header header;
    const qlcp_control_data *control_data;
    uint8_t control_count;
    uint8_t device_status;
} qlcp_status_packet;

typedef struct {
    qlcp_header header;
    const qlcp_sensor_data *sensor_data;
    uint8_t sensor_count;
} qlcp_data_packet;

typedef struct {
    qlcp_header header;
    const char *config_data;
    uint32_t config_data_len;
} qlcp_config_packet;

// Payload tagged unions

typedef struct {
    qlcp_control_data *control_data;
    uint8_t control_data_len;
    qlcp_sensor_data *sensor_data;
    uint8_t sensor_data_len;
    char *config_data;
    uint32_t config_data_len;
} qlcp_server_payload_buffers;

typedef struct {
    qlcp_packet_type packet_type;
    union {
        qlcp_config_packet config;
        qlcp_data_packet data;
        qlcp_status_packet status;
        qlcp_ack_packet ack;
        qlcp_nack_packet nack;
    } payload_data;
} qlcp_server_payload;

typedef struct {
    qlcp_packet_type packet_type;
    union {
        qlcp_header_only_packet header_only;
        qlcp_control_packet control;
        qlcp_stream_start_packet stream_start;
        qlcp_ack_packet ack;
        qlcp_nack_packet nack;
    } payload_data;
} qlcp_client_payload;

//----------------------------------------------------------
// Packet structs to raw bytes:
// Takes a pointer to a packet struct, and
// fills buffer with the encoded packet.
// buffer_len should be a pointer to the length
// of buffer, which will update to the length of
// the packet in the buffer.
//----------------------------------------------------------

qlcp_lib_ret qlcp_encode_header_only(uint8_t buffer[], size_t *buffer_len, qlcp_packet_type packet_type, const qlcp_header_only_packet *header_only);

qlcp_lib_ret qlcp_encode_ack(uint8_t buffer[], size_t *buffer_len, const qlcp_ack_packet *ack);

qlcp_lib_ret qlcp_encode_nack(uint8_t buffer[], size_t *buffer_len, const qlcp_nack_packet *nack);

qlcp_lib_ret qlcp_encode_stream_start(uint8_t buffer[], size_t *buffer_len, const qlcp_stream_start_packet *stream_start);

qlcp_lib_ret qlcp_encode_control(uint8_t buffer[], size_t *buffer_len, const qlcp_control_packet *control);

// Status, data, and config packet structs include pointers to a variable length array.
// When these functions are called, the data at that address must still be intact.

qlcp_lib_ret qlcp_encode_status(uint8_t buffer[], size_t *buffer_len, const qlcp_status_packet *status);

qlcp_lib_ret qlcp_encode_data(uint8_t buffer[], size_t *buffer_len, const qlcp_data_packet *data);

qlcp_lib_ret qlcp_encode_config(uint8_t buffer[], size_t *buffer_len, const qlcp_config_packet *config);

//----------------------------------------------------------
// Raw bytes to packet struct:
// qlcp_decode_client_to_server should be used server-side,
// and qlcp_decode_server_to_client should be used client-side.
// Takes an encoded packet in buffer and decodes into
// payload, as a tagged union.
//----------------------------------------------------------

qlcp_lib_ret qlcp_get_packet_len(uint16_t *data_len, const uint8_t buffer[], size_t buffer_len);

qlcp_lib_ret qlcp_decode_client_to_server(qlcp_server_payload *payload, qlcp_server_payload_buffers *payload_buffers, const uint8_t buffer[], size_t buffer_len);

qlcp_lib_ret qlcp_decode_server_to_client(qlcp_client_payload *payload, const uint8_t buffer[], size_t buffer_len);

#endif