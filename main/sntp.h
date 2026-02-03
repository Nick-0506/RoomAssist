/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_sntp.h"

#ifdef __cplusplus
extern "C" {
#endif

// SNTP server configuration
#define SNTP_DEFAULT_SERVER "pool.ntp.org"

// SNTP synchronization interval (seconds)
#define SNTP_DEFAULT_SYNC_INTERVAL 3600
void task_sntpc(void *pvParameter);
#ifdef __cplusplus
}
#endif