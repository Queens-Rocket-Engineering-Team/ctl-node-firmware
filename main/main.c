#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include "packet_handler.h"
#include "sensor_stream.h"
#include "setup.h"
#include "board_setup.h"

#define SENSOR_STREAM_STACK_SIZE 4096
#define PACKET_HANDLER_STACK_SIZE 4096

static const char *TAG = "MAIN";

void app_main(void) {

    static app_ctx_t app_ctx = {0};

    ESP_ERROR_CHECK(app_setup(&app_ctx));
    ESP_LOGI(TAG, "App setup complete");

    board_ctx_t board_ctx = {
        .adcs = app_ctx.adcs,
        .num_adcs = CONFIG_NUM_ADCS,
    };
    ESP_ERROR_CHECK(board_setup(&board_ctx));

    // start sensor stream task
    static StaticTask_t xTaskBuffer_SENSORSTREAM;
    static StackType_t xStack_SENSORSTREAM[SENSOR_STREAM_STACK_SIZE];

    app_ctx.sensor_stream_handle = xTaskCreateStatic(
        sensor_stream,
        "Sensor Stream",
        SENSOR_STREAM_STACK_SIZE,
        (void *) &app_ctx,
        1,
        xStack_SENSORSTREAM,
        &xTaskBuffer_SENSORSTREAM
    );

    // start packet handler task
    static StaticTask_t xTaskBuffer_PACKETHANDLER;
    static StackType_t xStack_PACKETHANDLER[PACKET_HANDLER_STACK_SIZE];

    xTaskCreateStatic(
        packet_handler,
        "Packet Handler",
        PACKET_HANDLER_STACK_SIZE,
        (void *) &app_ctx,
        1,
        xStack_PACKETHANDLER,
        &xTaskBuffer_PACKETHANDLER
    );
}
