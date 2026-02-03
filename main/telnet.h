/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

/* For telnet server */
#include "esp_netif.h"
#include "lwip/sockets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Telneet server */
#define TELNET_PORT 23

extern int gtelnet_server_listen_sock;
extern int gtelnet_server_acceptsock;
void task_telnet(void *pvParameters);

#ifdef __cplusplus
}
#endif