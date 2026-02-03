/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"
#include "esp_system.h"
#include "system.h"
#include "thingspeak.h"
#include "dht22.h"
#include "airquality.h"
#include "syslog.h"
SemaphoreHandle_t gsemaTSPKCfg = NULL;
char gtpapikey[THINGSPEAK_API_KEYLENGTH + 1] = {0};

int thingspeak_getapikey(char *str, int length)
{
  if (gsemaTSPKCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_THINGSPEAK, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (thingspeak %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (str == NULL)
  {
    return SYSTEM_ERROR_INVALID_POINTER;
  }
  if (xSemaphoreTake(gsemaTSPKCfg, portMAX_DELAY) == pdTRUE)
  {
    strncpy(str, gtpapikey, length);
    xSemaphoreGive(gsemaTSPKCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int thingspeak_setapikey(char *str, int length)
{
  if (gsemaTSPKCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_THINGSPEAK, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (thingspeak %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (str == NULL)
  {
    return SYSTEM_ERROR_INVALID_POINTER;
  }
  if (length > THINGSPEAK_API_KEYLENGTH)
  {
    return SYSTEM_ERROR_INVALID_PARAMETER;
  }
  if (xSemaphoreTake(gsemaTSPKCfg, portMAX_DELAY) == pdTRUE)
  {
    strncpy(gtpapikey, str, length);
    xSemaphoreGive(gsemaTSPKCfg);
  }
  return SYSTEM_ERROR_NONE;
}

static void thingspeak_restoreconfig(void);

void task_thingspeak(void *pvParameters)
{
  float temperature = 0, humidity = 0;
  int mq135aqi = 0;
  int status = 0;
  char curtpapikey[THINGSPEAK_API_KEYLENGTH + 1] = {0};
  system_task_created(TASK_THINGSPEAK_ID);

  thingspeak_restoreconfig();

  system_task_all_ready();

  while (1)
  {
    thingspeak_getapikey(curtpapikey, THINGSPEAK_API_KEYLENGTH);
    if (strlen(curtpapikey) > 0)
    {
      char url[256];
      dht22_getcurrenthumidity(&humidity);
      dht22_getcurrenttemperature(&temperature);
      airquality_get_voc_index(&mq135aqi);
      snprintf(url, sizeof(url),
               "http://api.thingspeak.com/"
               "update?api_key=%s&field1=%d&field2=%.1f&field3=%.1f",
               curtpapikey, mq135aqi, temperature, humidity);

      esp_http_client_config_t config = {
          .url = url,
          .method = HTTP_METHOD_GET,
      };

      esp_http_client_handle_t client = esp_http_client_init(&config);
      esp_err_t err = esp_http_client_perform(client);

      if (err == ESP_OK)
      {
        status = esp_http_client_get_status_code(client);
        // ESP_LOGI(TAG, "HTTP POST Status = %d", status);
      }
      else
      {
        // ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
      }

      esp_http_client_cleanup(client);
    }
    vTaskDelay(15000 / portTICK_PERIOD_MS);  // 15秒更新一次
  }
}

static void thingspeak_restoreconfig(void)
{
  nvs_handle_t nvs_handle;
  esp_err_t ret;
  size_t length = THINGSPEAK_API_KEYLENGTH;
  char nvstpapikey[THINGSPEAK_API_KEYLENGTH + 1] = {0};

  gsemaTSPKCfg = xSemaphoreCreateBinary();
  if (gsemaTSPKCfg == NULL)
  {
    return;
  }
  ret = nvs_open(THINGSPEAK_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK)
  {
    syslog_handler(SYSLOG_FACILITY_THINGSPEAK, SYSLOG_LEVEL_ERROR,
                   "Open NVS fail when restore");
    xSemaphoreGive(gsemaTSPKCfg);
    return;
  }
  nvs_get_str(nvs_handle, THINGSPEAK_NVS_API_KEY, NULL, &length);
  ret = nvs_get_str(nvs_handle, THINGSPEAK_NVS_API_KEY, nvstpapikey, &length);
  if (ret != ESP_OK)
  {
    syslog_handler(SYSLOG_FACILITY_THINGSPEAK, SYSLOG_LEVEL_ERROR,
                   "Get API KEY fail");
  }
  else
  {
    strncpy(gtpapikey, nvstpapikey, THINGSPEAK_API_KEYLENGTH);
    syslog_handler(SYSLOG_FACILITY_THINGSPEAK, SYSLOG_LEVEL_INFO,
                   "API KEY %s restored", nvstpapikey);
  }
  nvs_close(nvs_handle);
  xSemaphoreGive(gsemaTSPKCfg);
  return;
}

void thingspeak_saveconfig(void)
{
  nvs_handle_t nvs_handle;
  char nvstpapikey[THINGSPEAK_API_KEYLENGTH + 1] = {0};

  esp_err_t ret;

  ret = nvs_open(THINGSPEAK_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK)
  {
    syslog_handler(SYSLOG_FACILITY_THINGSPEAK, SYSLOG_LEVEL_ERROR,
                   "Open NVS fail when save");
    return;
  }
  thingspeak_getapikey(nvstpapikey, THINGSPEAK_API_KEYLENGTH);
  ret = nvs_set_str(nvs_handle, THINGSPEAK_NVS_API_KEY, nvstpapikey);
  if (ret != ESP_OK)
  {
    syslog_handler(SYSLOG_FACILITY_THINGSPEAK, SYSLOG_LEVEL_ERROR,
                   "Set API KEY fail");
  }

  nvs_close(nvs_handle);
  syslog_handler(SYSLOG_FACILITY_THINGSPEAK, SYSLOG_LEVEL_INFO,
                 "API KEY %s saved", nvstpapikey);
  return;
}
