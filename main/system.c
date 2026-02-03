/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "system.h"
#include "syslog.h"

// static const char *TAG = "system";
char gsystem_creating_task[TASK_MAX_ID / 8 + 1] = {};
char gsystem_created_task[TASK_MAX_ID / 8 + 1] = {};
char SYSTEM_TaskName[TASK_MAX_ID + 1][SYSTEM_TASK_NAME_LENGTH + 1] = {
    "HOMEKIT", "UART", "LD2410", "DHT22",      "OTA",       "SNTPC",
    "TELNET",  "RMT",  "OLED",   "AirQuality", "THINKSPEAK"};

/* Debug message tag */
const char *TAG_LD2410 = "LD2410";
const char *TAG_IR = "IR";
const char *TAG_HK = "HomeKit";
const char *TAG_TEL = "telnet_server";
const char *TAG_NVS = "NVS";
QueueHandle_t gqueue_sysreboot;

/* System global var */
esp_netif_ip_info_t gsys_ip_info;
bool gsys_rebootstatus = SYSTEM_REBOOT_REBOOTING;
bool gsys_erasenvsstatus = 0;

SemaphoreHandle_t gsemaLD2410 =
    NULL;  // Created in app_main.c, control LD2410 idel and detect
SemaphoreHandle_t gsemaReboot = NULL;  // Created in app_main.c, control reboot
SemaphoreHandle_t gsemaLED =
    NULL;  // Created in app_main.c, means LED is setting
SemaphoreHandle_t gsemaRmtDeltaSche =
    NULL;  // Created in app_main.c, protect RMT delta scheduler
SemaphoreHandle_t gsemaRmtHitachiTig =
    NULL;  // Created in app_main.c, protect hitachi data
SemaphoreHandle_t gsemaRmtZeroTig =
    NULL;  // Created in app_main.c, protect zero fan data
SemaphoreHandle_t gsemaSYSTEMCfg =
    NULL;  // Created in app_main.c, protect system data

bool system_isrebooting()
{
  bool ret = 0;
  if (gsemaSYSTEMCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (system %d)", __LINE__);
    /* Semaphore not ready means rebooting */
    return true;
  }
  if (xSemaphoreTake(gsemaSYSTEMCfg, portMAX_DELAY) == pdTRUE)
  {
    ret = gsys_rebootstatus;
    xSemaphoreGive(gsemaSYSTEMCfg);
  }
  return ret;
}

int system_setrebooting(bool value)
{
  if (gsemaSYSTEMCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (system %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaSYSTEMCfg, portMAX_DELAY) == pdTRUE)
  {
    gsys_rebootstatus = value;
    xSemaphoreGive(gsemaSYSTEMCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int system_set_ip(esp_netif_ip_info_t *ip)
{
  if (gsemaSYSTEMCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (system %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (ip == NULL)
  {
    return SYSTEM_ERROR_INVALID_POINTER;
  }
  if (xSemaphoreTake(gsemaSYSTEMCfg, portMAX_DELAY) == pdTRUE)
  {
    memcpy(&gsys_ip_info, ip, sizeof(esp_netif_ip_info_t));
    xSemaphoreGive(gsemaSYSTEMCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int system_get_ip(esp_netif_ip_info_t *ip)
{
  if (gsemaSYSTEMCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (system %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (ip == NULL)
  {
    return SYSTEM_ERROR_INVALID_POINTER;
  }
  if (xSemaphoreTake(gsemaSYSTEMCfg, portMAX_DELAY) == pdTRUE)
  {
    memcpy(ip, &gsys_ip_info, sizeof(esp_netif_ip_info_t));
    xSemaphoreGive(gsemaSYSTEMCfg);
  }
  return SYSTEM_ERROR_NONE;
}

bool system_iserasingnvs()
{
  bool ret = 0;
  if (gsemaSYSTEMCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (system %d)", __LINE__);
    return false;
  }
  if (xSemaphoreTake(gsemaSYSTEMCfg, portMAX_DELAY) == pdTRUE)
  {
    ret = gsys_erasenvsstatus;
    xSemaphoreGive(gsemaSYSTEMCfg);
  }
  return ret;
}

int system_seterasingnvs(bool value)
{
  if (gsemaSYSTEMCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (system %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaSYSTEMCfg, portMAX_DELAY) == pdTRUE)
  {
    gsys_erasenvsstatus = value;
    xSemaphoreGive(gsemaSYSTEMCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int dbg_printf(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len > 0)
  {
    printf(buf);
  }

  return len;
}

void system_init_spiffs(void)
{
  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true,
  };

  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK)
  {
    ESP_LOGE("SPIFFS", "SPIFFS init failed: %s", esp_err_to_name(ret));
  }
  else
  {
    ESP_LOGI("SPIFFS", "SPIFFS mounted successfully");
  }
}

void system_reboot(void)
{
  int sys_msg = 1;
  gsys_rebootstatus = SYSTEM_REBOOT_REBOOTING;
  xQueueSend(gqueue_sysreboot, &sys_msg, portMAX_DELAY);
  return;
}

void system_task_all_ready(void)
{
  // char *task_name = pcTaskGetName(NULL);
  while (memcmp(gsystem_creating_task, gsystem_created_task,
                sizeof(gsystem_created_task)) != 0)
  {
    // dbg_printf("\n %10s Waiting all task ready %d\n",task_name);
    system_task_waiting();
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
  // dbg_printf("\n [OS TRN] %10s is running\n",task_name);
  return;
}

void system_task_created(char id)
{
  // char *task_name = pcTaskGetName(NULL);
  gsystem_created_task[id / 8] =
      gsystem_created_task[id / 8] | (0x01 << id % 8);
  // dbg_printf("\n [OS TDN] %10s is done %d %d
  // %02x\n",task_name,id,id/8,gsystem_created_task[id/8]);
  return;
}

void system_task_creating(char id)
{
  gsystem_creating_task[id / 8] =
      gsystem_creating_task[id / 8] | (0x01 << id % 8);
  // dbg_printf("\n [OS CRT] init %d %d %02x\n",id,
  // id/8,gsystem_created_task[id/8]);
  return;
}

int system_task_is_ready(char id)
{
  if (gsystem_creating_task[id / 8] & (0x01 << id % 8))
  {
    if (gsystem_created_task[id / 8] & (0x01 << id % 8))
    {
      return SYSTEM_TASK_INIT_DONE;
    }
    return SYSTEM_TASK_INITING;
  }
  else
  {
    return SYSTEM_TASK_NONE;
  }
}

void system_task_waiting(void)
{
  int k = 0, j = 0;
  char waiting = 0;
  // char *task_name = pcTaskGetName(NULL);
  for (k = 0; k < (TASK_MAX_ID / 8 + 1); k++)
  {
    waiting = gsystem_creating_task[k] ^ gsystem_created_task[k];
    for (j = 0; j < 8; j++)
    {
      if (waiting & (0x01 << j % 8))
      {
        // dbg_printf("\n %10s Waiting Task[%d] %02x %02x
        // %s\n",task_name,k,gsystem_creating_task[k],gsystem_created_task[k],SYSTEM_TaskName[k*8+j%8]);
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  return;
}
