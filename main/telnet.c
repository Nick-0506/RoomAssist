/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "telnet.h"
#include "system.h"
#include "syslog.h"

int gtelnet_server_listen_sock = -1;
int gtelnet_server_acceptsock = -1; 

void task_telnet(void *pvParameters) 
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    //printf("\n Telnet task is created.\n");
    system_task_created(TASK_TELNET_ID);
    system_task_all_ready();
    while (1) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(TELNET_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        gtelnet_server_listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (gtelnet_server_listen_sock < 0) {
            ESP_LOGE(TAG_TEL, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG_TEL, "Socket created");

        int err = bind(gtelnet_server_listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG_TEL, "Socket unable to bind: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG_TEL, "Socket bound, port %d", TELNET_PORT);

        err = listen(gtelnet_server_listen_sock, 1);
        if (err != 0) {
            ESP_LOGE(TAG_TEL, "Error occurred during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG_TEL, "Socket listening");

        while (1) {
            struct sockaddr_in6 source_addr;
            socklen_t addr_len = sizeof(source_addr);
            gtelnet_server_acceptsock = accept(gtelnet_server_listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (gtelnet_server_acceptsock < 0) {
                ESP_LOGE(TAG_TEL, "Unable to accept connection: errno %d", errno);
                break;
            }
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
            ESP_LOGI(TAG_TEL, "Socket accepted ip address: %s", addr_str);

            while (1) {
                int len = recv(gtelnet_server_acceptsock, rx_buffer, sizeof(rx_buffer) - 1, 0);
                if (len < 0) {
                    ESP_LOGE(TAG_TEL, "recv failed: errno %d", errno);
                    break;
                } else if (len == 0) {
                    ESP_LOGI(TAG_TEL, "Connection closed");
                    break;
                } else {
                    rx_buffer[len] = 0;
                    ESP_LOGI(TAG_TEL, "Received %d bytes: %s", len, rx_buffer);

                    // Echo back the received data
                    int to_write = len;
                    while (to_write > 0) {
                        int written = send(gtelnet_server_acceptsock, rx_buffer + (len - to_write), to_write, 0);
                        if (written < 0) {
                            ESP_LOGE(TAG_TEL, "Error occurred during sending: errno %d", errno);
                            break;
                        }
                        to_write -= written;
                    }
                }
            }

            shutdown(gtelnet_server_acceptsock, 0);
            close(gtelnet_server_acceptsock);
        }

        if (gtelnet_server_listen_sock != -1) {
            ESP_LOGE(TAG_TEL, "Shutting down socket and restarting...");
            shutdown(gtelnet_server_listen_sock, 0);
            close(gtelnet_server_listen_sock);
        }
    }
    vTaskDelete(NULL);
}