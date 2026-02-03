/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

// Task Configuration
#define AIRQUALITY_TASK_STACK_SIZE 4096
#define AIRQUALITY_TASK_PRIORITY 5
#define AIRQUALITY_TASK_NAME "airquality"

// Timing Configurations
#define AQ_IDLETIME 2000  // Task Loop Delay (ms)

// Function Prototypes
void task_airquality(void *arg);

// API for Webpages / External usage
int airquality_get_voc_index(int *value);
int airquality_set_voc_index(int value);

int airquality_get_nox_index(int *value);
int airquality_set_nox_index(int value);

// Threshold Management API
// VOC Thresholds (Legacy MQ135 High/Low keys)
int airquality_get_voc_threshold_high(int *value);
int airquality_set_voc_threshold_high(int value);

int airquality_get_voc_threshold_low(int *value);
int airquality_set_voc_threshold_low(int value);

// NOx Thresholds
int airquality_get_nox_threshold_high(int *value);
int airquality_set_nox_threshold_high(int value);

int airquality_get_nox_threshold_low(int *value);
int airquality_set_nox_threshold_low(int value);

void airquality_reset_baseline(void);
