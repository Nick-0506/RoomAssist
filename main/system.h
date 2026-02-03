/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include "esp_log.h"
#include <esp_log.h>
#include "esp_spiffs.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Task INFO */
#define TASK_HOMEKIT_ID          0
#define TASK_UART_ID             1
#define TASK_LD2410_ID           2
#define TASK_DHT22_ID            3
#define TASK_OTA_ID              4
#define TASK_SNTPC_ID            5
#define TASK_TELNET_ID           6
#define TASK_RMT_ID              7
#define TASK_OLED_ID             8
#define TASK_MQ135_ID            9
#define TASK_THINGSPEAK_ID       10
#define TASK_MAX_ID              TASK_THINGSPEAK_ID

#define SYSTEM_TASK_INIT_DONE   1
#define SYSTEM_TASK_INITING     2
#define SYSTEM_TASK_NONE        3
#define SYSTEM_TASK_NAME_LENGTH 32

#define SYSTEM_REBOOT_DONE      0
#define SYSTEM_REBOOT_REBOOTING 1

#define SYSTEM_ERROR_NONE               0
#define SYSTEM_ERROR_NOT_READY          1
#define SYSTEM_ERROR_INVALID_POINTER    2
#define SYSTEM_ERROR_INVALID_PARAMETER  3
/* CFG */
#define CFG_KEY_LENGTH      10

extern char gsystem_creating_task[TASK_MAX_ID/8+1];
extern char gsystem_created_task[TASK_MAX_ID/8+1];
extern char SYSTEM_TaskName[TASK_MAX_ID+1][SYSTEM_TASK_NAME_LENGTH+1];

/* Debug message tag */
extern const char *TAG_LD2410;
extern const char *TAG_IR;
extern const char *TAG_HK;
extern const char *TAG_TEL;
extern const char *TAG_NVS;
extern QueueHandle_t gqueue_sysreboot;

/* System global var */
extern SemaphoreHandle_t gsemaLD2410;
extern SemaphoreHandle_t gsemaReboot;
extern SemaphoreHandle_t gsemaLED;
extern SemaphoreHandle_t gsemaRmtDeltaSche;
extern SemaphoreHandle_t gsemaRmtHitachiTig;
extern SemaphoreHandle_t gsemaRmtZeroTig;
extern SemaphoreHandle_t gsemaSYSTEMCfg;

int dbg_printf(const char *fmt, ...);
void system_reboot(void);
void system_task_all_ready(void);
void system_task_created(char id);
void system_task_creating(char id);
int system_task_is_ready(char id);
void system_task_waiting(void);
void system_list_tasks(void);
void system_init_spiffs(void);
bool system_isrebooting();
int system_setrebooting(bool );
bool system_iserasingnvs();
int system_seterasingnvs(bool );
int system_get_ip(esp_netif_ip_info_t *ip);
int system_set_ip(esp_netif_ip_info_t *ip);

#ifdef __cplusplus
}
#endif