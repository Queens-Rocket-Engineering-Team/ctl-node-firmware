#include "wifi_tools.h"
#include <esp_err.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>

#define SSDP_PORT "1900"
#define SSDP_IP "239.255.255.250"
#define SSDP_ANY_IP "0.0.0.0"

static const char *TAG = "SSDP";

esp_err_t ssdp_discover_server(int32_t *sock, char server_ip[], size_t server_ip_len, esp_netif_t *netif_handle) {
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

    err = getaddrinfo(SSDP_ANY_IP, SSDP_PORT, &hints, &res);
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

    // bind the socket to port 1900, any ip
    err = bind(*sock, res->ai_addr, res->ai_addrlen);
    if (err != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    // add membership to SSDP multicast ip
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_get_ip_info(netif_handle, &ip_info);

    struct in_addr local_addr;
    inet_addr_from_ip4addr(&local_addr, &ip_info.ip);

    ip_mreq imreq = {0};
    imreq.imr_interface.s_addr = local_addr.s_addr;
    err = inet_pton(AF_INET, SSDP_IP, &imreq.imr_multiaddr.s_addr);
    if (err != 1) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    err = setsockopt(*sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof imreq);
    if (err != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    // listen for SSDP M-SEARCH from server
    struct sockaddr_in remote_addr = {0};
    socklen_t remote_addr_len;
    static char buffer[1024];

    while (1) {
        remote_addr_len = sizeof remote_addr;
        ssize_t len = recvfrom(*sock, buffer, sizeof buffer - 1, 0, (struct sockaddr *)&remote_addr, &remote_addr_len);

        if (len < 0) {
            ret = ESP_FAIL;
            goto cleanup;
        }

        buffer[len] = '\0'; // recvfrom does not null terminate buffer

        ESP_LOGD(TAG, "%s", buffer);

        // check if received data matches server SSDP request
        if (strcasestr(buffer, "M-SEARCH * HTTP/1.1") != NULL &&
            strcasestr(buffer, "HOST: 239.255.255.250:1900") != NULL &&
            strcasestr(buffer, "ST: urn:qretprop:espdevice:1") != NULL) {
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