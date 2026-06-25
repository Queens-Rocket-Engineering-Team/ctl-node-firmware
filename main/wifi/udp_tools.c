#include <esp_err.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>

#include "qlcp_lib.h"
#include "wifi_tools.h"

#define TX_BUFFER_LEN 2048

static const char *TAG = "UDP";

esp_err_t udp_create_socket(int32_t *sock, const char server_ip[], uint16_t server_port) {
    if (sock == NULL || server_ip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char server_port_str[10] = {0};
    snprintf(server_port_str, sizeof(server_port_str), "%u", server_port);

    esp_err_t ret = ESP_FAIL;
    *sock = -1;

    int32_t err;

    struct addrinfo hints = {0}, *res = NULL;
    // set up UDP parameters for socket
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    err = getaddrinfo(server_ip, server_port_str, &hints, &res);
    if (err != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    // creating client's socket
    *sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (*sock < 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    // link udp port to server so send() can be used instead of sendto()
    // no packet is sent here unlike connect() with tcp
    err = connect(*sock, res->ai_addr, res->ai_addrlen);
    if (err != 0) {
        ESP_LOGE(TAG, "connect failed: errno %d", errno);
        goto cleanup;
    }
    ESP_LOGI(TAG, "Connected to server");
    ret = ESP_OK;

cleanup:
    if (res != NULL) {
        freeaddrinfo(res);
    }
    if (*sock != -1 && ret != ESP_OK) {
        close(*sock);
        *sock = -1;
    }
    return ret;
}

void udp_client_send(void *pvParams) {
    network_ctx_t *network_ctx = (network_ctx_t *)pvParams;

    static uint8_t tx_buffer[TX_BUFFER_LEN] = {0};
    static qlcp_data_packet data_packet;

    qlcp_lib_ret ret = QLCP_OK;

    while (1) {
        // only pause when server is disconnected
        xEventGroupWaitBits(network_ctx->wifi_event_group_handle, SERVER_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

        // timeout after 100ms to check if server is still alive
        if (xQueueReceive(network_ctx->udp_send_queue_handle, &data_packet, pdMS_TO_TICKS(100)) == pdFALSE) {
            continue;
        }

        size_t packet_len = sizeof(tx_buffer);

        // convert packet struct to bytes
        ret = qlcp_encode_data(tx_buffer, &packet_len, &data_packet);
        if (ret != QLCP_OK) {
            ESP_LOGE(TAG, "QLCP err:", ret);
        }

        int32_t len_sent = send(network_ctx->server_udp_sock, tx_buffer, packet_len, 0);
        // give semaphore to allow data to be modified after sent
        xSemaphoreGive(network_ctx->udp_send_semaphore_handle);

        if (len_sent < 0) {
            ESP_LOGE(TAG, "send failed: errno %d", errno);
            continue;
        }
        ESP_LOGD(TAG, "Outgoing packet len: %d", len_sent);
        ESP_LOG_BUFFER_HEXDUMP(TAG, tx_buffer, len_sent, ESP_LOG_DEBUG);
    }
}
