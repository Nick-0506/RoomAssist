/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include <esp_https_ota.h>


#ifdef __cplusplus
extern "C" {
#endif

#define OTA_NVS_NAMESPACE       "ota"
#define OTA_NVS_STATUS_KEY      "ota_status"
#define OTA_NVS_SERVER_IP       "serverip"
#define OTA_NVS_FILENAME        "filename"

#define OTA_DEFAULT_SERVER_IP   "192.168.50.137"
#define OTA_DEFAULT_FILENAME    "emulator.bin"
#define OTA_MAXLEN_IP           15
#define OTA_MAXLEN_FILENAME     32

typedef enum {
    OTA_IDLE,
    OTA_IN_PROGRESS,
    OTA_DONE
} ota_status_t;

extern QueueHandle_t gqueue_ota;

void ota_abort(void);
void ota_restoreconfig(void);
void ota_saveconfig(char *key, char *str);
void task_ota(void *pvParameter);
int ota_setstatus(uint8_t);
int ota_getstatus(uint8_t *);
int ota_setprogress(int);
int ota_getprogress(int *);
int ota_setcontent_len(int);
int ota_getcontent_len(int *);
int ota_settotal_readlen(int);
int ota_gettotal_readlen(int *);
int ota_setfilename(char *, int);
int ota_getfilename(char *, int);
int ota_setip(char *, int);
int ota_getip(char *, int);
#ifdef __cplusplus
}
#endif