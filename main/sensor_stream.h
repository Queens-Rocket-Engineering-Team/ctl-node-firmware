#pragma once

#define SENSOR_STREAM_STACK_SIZE 4096

#define SENSOR_STREAM_ENABLE_BIT 1
#define SENSORS_SINGLE_READING_BIT (1 << 1)

void sensor_stream(void *pvParams);