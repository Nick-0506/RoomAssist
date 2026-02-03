/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_wifi.h>
#include "sntp.h"
#include "syslog.h"
#include "system.h"

void task_sntpc(void *pvParameter)
{
    time_t now = 0;
    struct tm timeinfo = { 0 };
    
    // Wait for the connection to the WiFi network
    ESP_ERROR_CHECK(esp_wifi_connect());

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, SNTP_DEFAULT_SERVER);
    esp_sntp_init();

    // Wait for synchronization to complete
    while (timeinfo.tm_year < (2016 - 1900)) {
        vTaskDelay((10*1000) / portTICK_PERIOD_MS); // Per 10 sec
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    // Set timezone to GMT+8 (Beijing Time)
    setenv("TZ", "CST-8", 1);
    tzset();
    // SNTP synchronization complete

    system_task_created(TASK_SNTPC_ID);
    system_task_all_ready();

    while (1) 
    {
        syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO,"Free Heap: %d,%03d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT)/1000,heap_caps_get_free_size(MALLOC_CAP_8BIT)%1000);
        vTaskDelay((24*60*60*1000) / portTICK_PERIOD_MS); // Per 1 Day
    }
}