#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "packet_handler.h"
#include "config_json.h"
#include "control.h"
#include "qlcp_lib.h"
#include "sensor_stream.h"
#include "setup.h"
#include "wifi_tools.h"

#define PACKET_HANDLER_STACK_SIZE 4096

static const char *TAG = "MAIN";

void app_main(void) {

    static app_ctx_t app_ctx = {0};

    ESP_ERROR_CHECK(app_setup(&app_ctx));
    ESP_LOGI(TAG, "Setup complete");

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
