/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS products only, in
 * which case, it is free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the
 * Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/* HomeKit Emulator Example
 */

// clang-format off
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <app_wifi.h>
#include <hap_bct_http_handlers.h>
#include <esp_log.h>
#include <esp_mac.h>
// clang-format on

#include "dht22.h"
#include "driver/timer.h"
#include "elf.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "homekit.h"
#include "ld2410.h"
#include "max9814.h"
#include "airquality.h"
#include "oled.h"
#include "ota.h"
#include "rmt.h"
#include "sntp.h"
#include "syslog.h"
#include "system.h"
#include "task_monitor.h"
#include "telnet.h"
#include "thingspeak.h"
#include "webpages.h"

// Define Watchdog time 30 seconds
#define WDT_TIMEOUT_S 30

void app_main()
{
  esp_err_t ret = 0;
  nvs_handle_t nvs_handle;
  esp_netif_t *netif = NULL;
  int ota_msg = 0, sys_msg = 0;
  uint8_t sys_mac[6], ota_status = OTA_DONE;
  char ota_ip[OTA_MAXLEN_IP + 1], ota_filename[OTA_MAXLEN_FILENAME + 1];
  esp_netif_ip_info_t sys_ip_info;

  memset(gsystem_creating_task, 0, sizeof(gsystem_creating_task));
  memset(gsystem_created_task, 0, sizeof(gsystem_created_task));
  memset(ota_ip, 0, sizeof(ota_ip));
  memset(ota_filename, 0, sizeof(ota_filename));

  srand(time(NULL));

  // Add watch dog on app_main, httpd, mdns task
  esp_task_wdt_add(NULL);

  // Initialize NVS
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize SPIFFS
  system_init_spiffs();

  // Initialize Wi-Fi
  app_wifi_init();

  // ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, gsys_mac));

  // Restore Syslog configuration
  syslog_restoreconfig();

  // Restore OTA configuration
  ota_restoreconfig();
  ota_getfilename(ota_filename, OTA_MAXLEN_FILENAME);
  ota_getip(ota_ip, OTA_MAXLEN_IP);
  if (strlen(ota_ip) == 0)
  {
    strcpy(ota_ip, OTA_DEFAULT_SERVER_IP);
  }
  if (strlen(ota_filename) == 0)
  {
    strcpy(ota_filename, OTA_DEFAULT_FILENAME);
  }

  // Initialize Queue
  gqueue_ota = xQueueCreate(1, sizeof(int));
  assert(gqueue_ota);

  gqueue_sysreboot = xQueueCreate(1, sizeof(int));
  assert(gqueue_sysreboot);

  // Initialize Semaphore
  gsemaReboot = xSemaphoreCreateBinary();
  if (gsemaReboot != NULL)
  {
    xSemaphoreGive(gsemaReboot);
  }

  gsemaLED = xSemaphoreCreateBinary();
  if (gsemaLED != NULL)
  {
    xSemaphoreGive(gsemaLED);
  }

  gsemaLD2410 = xSemaphoreCreateBinary();
  if (gsemaLD2410 != NULL)
  {
    xSemaphoreGive(gsemaLD2410);
  }

  gsemaRmtDeltaSche = xSemaphoreCreateBinary();
  if (gsemaRmtDeltaSche != NULL)
  {
    xSemaphoreGive(gsemaRmtDeltaSche);
  }

  gsemaRmtHitachiTig = xSemaphoreCreateBinary();
  if (gsemaRmtHitachiTig != NULL)
  {
    xSemaphoreGive(gsemaRmtHitachiTig);
  }

  gsemaRmtZeroTig = xSemaphoreCreateBinary();
  if (gsemaRmtZeroTig != NULL)
  {
    xSemaphoreGive(gsemaRmtZeroTig);
  }

  gsemaSYSTEMCfg = xSemaphoreCreateBinary();
  if (gsemaSYSTEMCfg != NULL)
  {
    xSemaphoreGive(gsemaSYSTEMCfg);
  }

  // Create Homekit Task
  system_task_creating(TASK_HOMEKIT_ID);
  xTaskCreate(task_homekit_init, HAP_ACC_TASK_NAME, HAP_ACC_TASK_STACKSIZE,
              NULL, HAP_ACC_TASK_PRIORITY, NULL);

  // Create OLED Task
  system_task_creating(TASK_OLED_ID);
  xTaskCreate(task_oled, SYSTEM_TaskName[TASK_OLED_ID], 5120, NULL, 5, NULL);

  while (system_task_is_ready(TASK_HOMEKIT_ID) != SYSTEM_TASK_INIT_DONE)
  {
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    dbg_printf("\n Waiting Homekit Task init\n");
  }

  // Initialize UART and LD2410
  ld2410_uart_init();

  // Create UART task to handler UART event from ISR
  system_task_creating(TASK_UART_ID);
  xTaskCreate(task_uart_event, SYSTEM_TaskName[TASK_UART_ID], 4096, NULL, 12,
              NULL);

  http_server_start();

  // Create LD2410 Task
  system_task_creating(TASK_LD2410_ID);
  xTaskCreate(task_ld2410, SYSTEM_TaskName[TASK_LD2410_ID], (1024 * 4), NULL, 5,
              NULL);

  // Create SNTP client Task
  system_task_creating(TASK_SNTPC_ID);
  xTaskCreate(task_sntpc, SYSTEM_TaskName[TASK_SNTPC_ID], 4096, NULL, 5, NULL);

  // Create DHT22 Task
  system_task_creating(TASK_DHT22_ID);
  xTaskCreate(task_dht22, SYSTEM_TaskName[TASK_DHT22_ID], 4096, NULL, 5, NULL);

  // Create Telnet Task
  system_task_creating(TASK_TELNET_ID);
  xTaskCreate(task_telnet, SYSTEM_TaskName[TASK_TELNET_ID], 4096, NULL, 5,
              NULL);

  // Create RMT Task
  system_task_creating(TASK_RMT_ID);
  xTaskCreate(task_rmt, SYSTEM_TaskName[TASK_RMT_ID], 4096, NULL, 5, NULL);

  ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
  if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
  {
    // Create Air Quality Task (Replaces MQ135 Task)
    system_task_creating(TASK_MQ135_ID);
    xTaskCreate(task_airquality, SYSTEM_TaskName[TASK_MQ135_ID],
                AIRQUALITY_TASK_STACK_SIZE, NULL, AIRQUALITY_TASK_PRIORITY,
                NULL);
  }

  // Create ThinkSpeak Task
  system_task_creating(TASK_THINGSPEAK_ID);
  xTaskCreate(task_thingspeak, SYSTEM_TaskName[TASK_THINGSPEAK_ID], 4096, NULL,
              5, NULL);

  // Create Monitor Task
  xTaskCreate(task_monitor, "monitor", 4096, NULL, 1, NULL);

  // Check OTA status
  ret = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret == ESP_OK)
  {
    nvs_get_u8(nvs_handle, OTA_NVS_STATUS_KEY, &ota_status);
    nvs_close(nvs_handle);
  }
  else
  {
    ota_status = OTA_DONE;
  }

  if (ota_status != OTA_DONE)
  {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_WARNING,
                   "OTA not complete, Upgrade firmware then reboot");
    system_task_creating(TASK_OTA_ID);
    xTaskCreate(task_ota, "task_ota", 8192, NULL, 5, NULL);
    xQueueSend(gqueue_ota, &ota_msg, portMAX_DELAY);
  }

  // Init MAX9814 and FFT
  max9814_setup();

  // Get IP address information
  netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_get_ip_info(netif, &sys_ip_info);
  system_set_ip(&sys_ip_info);
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason)
  {
    case ESP_RST_POWERON:
      syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO, "Cold Start");
      break;
    case ESP_RST_SW:
      syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO, "Warm Start");
      break;
    case ESP_RST_WDT:
      syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO, "Warm Start");
      break;
    case ESP_RST_DEEPSLEEP:
      syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO, "Weakup");
      break;
    case ESP_RST_BROWNOUT:
      syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                     "Power failure");
      break;
    default:
      syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                     "Other reboot");
      break;
  }
  dbg_printf("\n\n System Info\n");
  dbg_printf(" Free heap size: %ldKB\n", esp_get_free_heap_size() / 1024);
  dbg_printf(" Build Version: %s (%s)\n", TOSTRING(BUILD_VERSION),
             TOSTRING(BUILD_TIME));
  dbg_printf(" Device IP Address: " IPSTR "\n", IP2STR(&sys_ip_info.ip));

  // rebooting done
  system_setrebooting(false);
  syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO,
                 "System running...");

  while (1)
  {
    while (xQueueReceive(gqueue_sysreboot, &sys_msg, pdMS_TO_TICKS(1000)) !=
           pdPASS)
    {
      esp_task_wdt_reset();
    }

    if (sys_msg == 1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      if ((xSemaphoreTake(gsemaReboot, portMAX_DELAY) == pdTRUE) &&
          (xSemaphoreTake(gsemaLED, portMAX_DELAY) == pdTRUE))
      {
        syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR,
                       "System reboot...");
        esp_restart();
      }
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}
