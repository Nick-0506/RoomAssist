/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "syslog.h"
#include "system.h"

#define MONITOR_TAG "MONITOR"

void task_monitor(void *pvParameters)
{
    TaskStatus_t *pxTaskStatusArray;
    volatile UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime;

    // Wait for system stabilization
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        // Take a snapshot of the number of tasks in case it changes while we are accessing the list.
        uxArraySize = uxTaskGetNumberOfTasks();

        // Allocate a TaskStatus_t structure for each task.
        pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));

        if (pxTaskStatusArray != NULL) {
            // Generate raw status information about each task.
            uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

            syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO, "--- Task Memory Monitor ---");
            syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO, "%-20s %-10s %-10s", "Task Name", "State", "Min Stack Free (Bytes)");

            for (x = 0; x < uxArraySize; x++) {
                char state_char;
                switch (pxTaskStatusArray[x].eCurrentState) {
                    case eRunning:   state_char = 'X'; break;
                    case eReady:     state_char = 'R'; break;
                    case eBlocked:   state_char = 'B'; break;
                    case eSuspended: state_char = 'S'; break;
                    case eDeleted:   state_char = 'D'; break;
                    case eInvalid:   state_char = 'I'; break;
                    default:         state_char = '?'; break;
                }
                
                // StackHighWaterMark is in *words* on some ports, but ESP-IDF FreeRTOS usually reports in Bytes (or we multiply by 4? ESP32 stack width is 1 byte, usually returns bytes).
                // Actually in ESP-IDF it returns Bytes.
                
                uint32_t free_stack = pxTaskStatusArray[x].usStackHighWaterMark; // * 4? Validated: ESP-IDF usStackHighWaterMark is bytes if using vanilla FreeRTOS, but ESP-IDF often modifies it. 
                // Wait, ESP-IDF FreeRTOS usStackHighWaterMark IS bytes.
                
                syslog_handler(SYSLOG_FACILITY_SYSTEM, (free_stack < 512) ? SYSLOG_LEVEL_WARNING : SYSLOG_LEVEL_INFO,
                               "%-20s %c          %u",
                               pxTaskStatusArray[x].pcTaskName,
                               state_char,
                               (unsigned int)free_stack);
            }
            syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_INFO, "---------------------------");
            
            // Free the array.
            vPortFree(pxTaskStatusArray);
        } else {
             syslog_handler(SYSLOG_FACILITY_SYSTEM, SYSLOG_LEVEL_ERROR, "Failed to allocate task monitor memory");
        }

        // Check every 30 seconds
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
