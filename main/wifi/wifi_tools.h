#pragma once

#include <esp_err.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <netdb.h>
#include <stdint.h>
#include <stdbool.h>

#define MESSAGE_QUEUE_TIMEOUT (pdMS_TO_TICKS(10))

#define SERVER_CONNECTED_BIT 1

enum {
    SIG_WIFI_DISCONN = (1 << 0),
    SIG_WIFI_CONN = (1 << 1),
    SIG_SERVER_DISCONN = (1 << 2),
    SIG_SERVER_RETRY = (1 << 3),
    SIG_SSDP_GOT_SERVER = (1 << 4),
    SIG_TCP_CONN_SERVER = (1 << 5),
};

typedef struct {
    esp_netif_t *netif_handle;
    char server_ip[IPADDR_STRLEN_MAX];
    uint16_t server_tcp_port;
    uint16_t server_udp_port;
    int32_t server_tcp_sock;
    int32_t server_udp_sock;
    int32_t ssdp_sock;
    QueueHandle_t tcp_recv_queue_handle;
    QueueHandle_t tcp_send_queue_handle;
    QueueHandle_t udp_send_queue_handle;
    SemaphoreHandle_t udp_send_semaphore_handle;
    EventGroupHandle_t wifi_event_group_handle;
    TaskHandle_t network_manager_handle;
    TaskHandle_t tcp_recv_handle;
    TaskHandle_t tcp_send_handle;
    TaskHandle_t udp_send_handle;
    bool config_sent;
} network_ctx_t;

esp_err_t wifi_event_handler_register(network_ctx_t *network_ctx);

esp_err_t network_manager_init(network_ctx_t *network_ctx);

esp_err_t ssdp_discover_server(int32_t *sock, char server_ip[], size_t server_ip_len, esp_netif_t *netif_handle);
esp_err_t tcp_connect_to_server(int32_t *sock, const char server_ip[], uint16_t server_port);
esp_err_t udp_create_socket(int32_t *sock, const char server_ip[], uint16_t server_port);

void network_state_manager(void *pvParams);

void tcp_client_recv(void *pvParams);
void tcp_client_send(void *pvParams);
void udp_client_send(void *pvParams);
