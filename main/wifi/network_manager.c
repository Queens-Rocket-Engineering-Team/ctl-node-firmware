#include <esp_check.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <netdb.h>
#include <sys/socket.h>

#include "config_json.h"
#include "qlcp_lib.h"
#include "wifi_tools.h"

#define TCP_SERVER_PORT 50000
#define UDP_SERVER_PORT 50001

#define NET_MANAGER_STACK_SIZE 4096
#define TCP_RECV_STACK_SIZE 4096
#define TCP_SEND_STACK_SIZE 4096
#define UDP_SEND_STACK_SIZE 4096

#define TCP_RECV_QUEUE_LEN 10
#define TCP_RECV_QUEUE_ITEM_SIZE sizeof(qlcp_client_payload)

#define TCP_SEND_QUEUE_LEN 10
#define TCP_SEND_QUEUE_ITEM_SIZE sizeof(qlcp_server_payload)

#define UDP_SEND_QUEUE_LEN 10
#define UDP_SEND_QUEUE_ITEM_SIZE sizeof(qlcp_data_packet)

static const char *TAG = "NETWORK MANAGER";

esp_err_t network_manager_init(network_ctx_t *network_ctx) {
    if (network_ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_ctx->server_tcp_port = TCP_SERVER_PORT;
    network_ctx->server_udp_port = UDP_SERVER_PORT;
    network_ctx->ssdp_sock = -1;
    network_ctx->server_tcp_sock = -1;
    network_ctx->server_udp_sock = -1;

    // set up the recv/send message queues
    static StaticQueue_t xStaticQueue_TCPSEND;
    static uint8_t ucQueueStorageArea_TCPSEND[TCP_SEND_QUEUE_LEN * TCP_SEND_QUEUE_ITEM_SIZE];

    network_ctx->tcp_send_queue_handle = xQueueCreateStatic(
        TCP_SEND_QUEUE_LEN, TCP_SEND_QUEUE_ITEM_SIZE, ucQueueStorageArea_TCPSEND, &xStaticQueue_TCPSEND
    );
    configASSERT(network_ctx->tcp_send_queue_handle);

    static StaticQueue_t xStaticQueue_TCPRECV;
    static uint8_t ucQueueStorageArea_TCPRECV[TCP_RECV_QUEUE_LEN * TCP_RECV_QUEUE_ITEM_SIZE];

    network_ctx->tcp_recv_queue_handle = xQueueCreateStatic(
        TCP_RECV_QUEUE_LEN, TCP_RECV_QUEUE_ITEM_SIZE, ucQueueStorageArea_TCPRECV, &xStaticQueue_TCPRECV
    );
    configASSERT(network_ctx->tcp_recv_queue_handle);

    static StaticQueue_t xStaticQueue_UDPSEND;
    static uint8_t ucQueueStorageArea_UDPSEND[UDP_SEND_QUEUE_LEN * UDP_SEND_QUEUE_ITEM_SIZE];

    network_ctx->udp_send_queue_handle = xQueueCreateStatic(
        UDP_SEND_QUEUE_LEN, UDP_SEND_QUEUE_ITEM_SIZE, ucQueueStorageArea_UDPSEND, &xStaticQueue_UDPSEND
    );
    configASSERT(network_ctx->udp_send_queue_handle);

    // set up binary semaphore for sensor data
    static StaticSemaphore_t xStaticSemaphoreBuffer_UDPSEND;
    network_ctx->udp_send_semaphore_handle = xSemaphoreCreateBinaryStatic(&xStaticSemaphoreBuffer_UDPSEND);
    configASSERT(network_ctx->udp_send_semaphore_handle);

    // set up event group for wifi connection status
    static StaticEventGroup_t xEventGroup_WIFI;

    network_ctx->wifi_event_group_handle = xEventGroupCreateStatic(&xEventGroup_WIFI);
    configASSERT(network_ctx->wifi_event_group_handle);

    // set up network tasks
    static StaticTask_t xTaskBuffer_NET;
    static StackType_t xStack_NET[NET_MANAGER_STACK_SIZE];

    network_ctx->network_manager_handle = xTaskCreateStatic(
        network_state_manager,
        "Network Manager",
        NET_MANAGER_STACK_SIZE,
        (void *)network_ctx,
        2,
        xStack_NET,
        &xTaskBuffer_NET
    );
    configASSERT(network_ctx->network_manager_handle);

    static StaticTask_t xTaskBuffer_TCPRECV;
    static StackType_t xStack_TCPRECV[TCP_RECV_STACK_SIZE];

    network_ctx->tcp_recv_handle = xTaskCreateStatic(
        tcp_client_recv,
        "Server TCP RECV",
        TCP_RECV_STACK_SIZE,
        (void *)network_ctx,
        1,
        xStack_TCPRECV,
        &xTaskBuffer_TCPRECV
    );
    configASSERT(network_ctx->tcp_recv_handle);

    static StaticTask_t xTaskBuffer_TCPSEND;
    static StackType_t xStack_TCPSEND[TCP_SEND_STACK_SIZE];

    network_ctx->tcp_send_handle = xTaskCreateStatic(
        tcp_client_send,
        "Server TCP SEND",
        TCP_SEND_STACK_SIZE,
        (void *)network_ctx,
        1,
        xStack_TCPSEND,
        &xTaskBuffer_TCPSEND
    );
    configASSERT(network_ctx->tcp_send_handle);

    static StaticTask_t xTaskBuffer_UDPSEND;
    static StackType_t xStack_UDPSEND[UDP_SEND_STACK_SIZE];

    network_ctx->udp_send_handle = xTaskCreateStatic(
        udp_client_send,
        "Server UDP SEND",
        UDP_SEND_STACK_SIZE,
        (void *)network_ctx,
        1,
        xStack_UDPSEND,
        &xTaskBuffer_UDPSEND
    );
    configASSERT(network_ctx->udp_send_handle);

#ifdef CONFIG_WIFI_INDICATOR_PIN

    ESP_RETURN_ON_ERROR(gpio_reset_pin(CONFIG_WIFI_INDICATOR_PIN), TAG, "Failed to reset wifi indicator GPIO");

    // set up wifi indicator led
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_WIFI_INDICATOR_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "GPIO config for wifi indicator failed");
    gpio_set_level(CONFIG_WIFI_INDICATOR_PIN, 0);

#endif

    return ESP_OK;
}

void network_state_manager(void *pvParams) {
    if (pvParams == NULL) {
        abort();
    }

    network_ctx_t *network_ctx = (network_ctx_t *)pvParams;
    uint32_t signal = SIG_WIFI_DISCONN;

    esp_err_t err;

    while (1) {
        xTaskNotifyWait(0, UINT32_MAX, &signal, portMAX_DELAY);

        if (signal & SIG_SERVER_DISCONN) {
            xTaskNotify(xTaskGetCurrentTaskHandle(), SIG_SERVER_RETRY, eSetBits);
            ESP_LOGE(TAG, "Server disconnected, retrying...");
        }
        if (signal & SIG_WIFI_DISCONN || signal & SIG_SERVER_DISCONN) {
            // reset state manager if wifi disconnects
            xEventGroupClearBits(network_ctx->wifi_event_group_handle, SERVER_CONNECTED_BIT);
            network_ctx->config_sent = false;

            if (network_ctx->ssdp_sock != -1) {
                close(network_ctx->ssdp_sock);
                network_ctx->ssdp_sock = -1;
            }
            if (network_ctx->server_tcp_sock != -1) {
                shutdown(network_ctx->server_tcp_sock, 0);
                close(network_ctx->server_tcp_sock);
                network_ctx->server_tcp_sock = -1;
            }
            if (network_ctx->server_udp_sock != -1) {
                close(network_ctx->server_udp_sock);
                network_ctx->server_udp_sock = -1;
            }
            continue;
        }
        if (signal & SIG_WIFI_CONN || signal & SIG_SERVER_RETRY) {
            // look for server when wifi connects
            err = ssdp_discover_server(
                &network_ctx->ssdp_sock, network_ctx->server_ip, IPADDR_STRLEN_MAX, network_ctx->netif_handle
            );
            if (err == ESP_OK) {
                xTaskNotify(xTaskGetCurrentTaskHandle(), SIG_SSDP_GOT_SERVER, eSetBits);
            } else {
                xTaskNotify(network_ctx->network_manager_handle, SIG_SERVER_DISCONN, eSetBits);
            }
        }
        if (signal & SIG_SSDP_GOT_SERVER) {
            // attempt to connect to server when ip found
            err = tcp_connect_to_server(
                &network_ctx->server_tcp_sock, network_ctx->server_ip, network_ctx->server_tcp_port
            );
            if (err == ESP_OK) {
                xTaskNotify(xTaskGetCurrentTaskHandle(), SIG_TCP_CONN_SERVER, eSetBits);
            } else {
                xTaskNotify(network_ctx->network_manager_handle, SIG_SERVER_DISCONN, eSetBits);
            }
        }
        if (signal & SIG_TCP_CONN_SERVER) {
            // create the udp socket for sending data packets
            udp_create_socket(&network_ctx->server_udp_sock, network_ctx->server_ip, network_ctx->server_udp_port);
            // enable the recv/send tasks when connected to server
            xEventGroupSetBits(network_ctx->wifi_event_group_handle, SERVER_CONNECTED_BIT);
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    TaskHandle_t *network_manager_task_handle = (TaskHandle_t *)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();

#ifdef CONFIG_WIFI_INDICATOR_PIN
            gpio_set_level(CONFIG_WIFI_INDICATOR_PIN, 0);
#endif

            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected from AP");
            xTaskNotify(*network_manager_task_handle, SIG_WIFI_DISCONN, eSetBits);

#ifdef CONFIG_WIFI_INDICATOR_PIN
            gpio_set_level(CONFIG_WIFI_INDICATOR_PIN, 0);
#endif

            esp_wifi_connect(); // TODO add retry counter
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to AP");
            break;
        }

    } else if (event_base == IP_EVENT) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "Got IP");
            if (network_manager_task_handle != NULL) {
                xTaskNotify(*network_manager_task_handle, SIG_WIFI_CONN, eSetBits);
            }

#ifdef CONFIG_WIFI_INDICATOR_PIN
            gpio_set_level(CONFIG_WIFI_INDICATOR_PIN, 1);
#endif

            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGI(TAG, "Lost IP");
            if (network_manager_task_handle != NULL) {
                xTaskNotify(*network_manager_task_handle, SIG_WIFI_DISCONN, eSetBits);
            }

#ifdef CONFIG_WIFI_INDICATOR_PIN
            gpio_set_level(CONFIG_WIFI_INDICATOR_PIN, 0);
#endif

            break;
        }
    }
}

esp_err_t wifi_event_handler_register(network_ctx_t *network_ctx) {

    esp_err_t ret;
    ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, &network_ctx->network_manager_handle, NULL
    );

    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, &network_ctx->network_manager_handle, NULL
    );

    return ret;
}
