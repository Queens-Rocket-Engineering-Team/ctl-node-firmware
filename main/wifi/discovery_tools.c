#include "wifi_tools.h"
#include "qlcp_lib.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>

#define DISCOVERY_PORT "10000"
#define DISCOVERY_IP "239.100.0.1"

static const char *TAG = "DISCOVERY";

esp_err_t discover_server(int32_t *sock, char server_ip[], size_t server_ip_len, esp_netif_t *netif_handle) {
    if (sock == NULL || server_ip == NULL || netif_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (server_ip_len < IPADDR_STRLEN_MAX) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_FAIL;
    *sock = -1;

    int32_t err;
    struct addrinfo hints = {0}, *res = NULL;
    // set up UDP parameters for socket
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    err = getaddrinfo("0.0.0.0", DISCOVERY_PORT, &hints, &res);
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

    int32_t enable = 1;
    err = setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof enable);
    if (err != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    err = bind(*sock, res->ai_addr, res->ai_addrlen);
    if (err != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    // add membership to discovery multicast
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_get_ip_info(netif_handle, &ip_info);

    struct in_addr local_addr;
    inet_addr_from_ip4addr(&local_addr, &ip_info.ip);

    ip_mreq imreq = {0};
    imreq.imr_interface.s_addr = local_addr.s_addr;
    err = inet_pton(AF_INET, DISCOVERY_IP, &imreq.imr_multiaddr.s_addr);
    if (err != 1) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    err = setsockopt(*sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof imreq);
    if (err != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    // listen for discovery packet from server
    struct sockaddr_in remote_addr = {0};
    socklen_t remote_addr_len;
    static uint8_t buffer[512];

    while (1) {
        remote_addr_len = sizeof(remote_addr);
        ssize_t len = recvfrom(*sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&remote_addr, &remote_addr_len);

        if (len < 0) {
            ret = ESP_FAIL;
            goto cleanup;
        }

        // check if received data matches server discovery request
        qlcp_client_payload payload = {0};
        if (qlcp_decode_server_to_client(&payload, buffer, len) != QLCP_OK) {
            continue;
        }

        if (payload.packet_type == QLCP_PT_DISCOVERY) {
            break;
        }
    }

    inet_ntop(remote_addr.sin_family, &remote_addr.sin_addr, server_ip, server_ip_len);

    ret = ESP_OK;

cleanup:
    if (res != NULL) {
        freeaddrinfo(res);
    }
    if (*sock != -1) {
        close(*sock);
        *sock = -1;
    }
    return ret;
}