/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAN_SCHEDULE_NVS_NAMESPACE "fan_sched"
#define FAN_SCHEDULE_NVS_INTERVAL  "interval_m"
#define FAN_SCHEDULE_NVS_RUN       "run_min"
#define FAN_SCHEDULE_NVS_ENABLED   "enabled"

#define FAN_SCHEDULE_DEFAULT_INTERVAL_MIN 30
#define FAN_SCHEDULE_DEFAULT_RUN_MIN      10

esp_err_t fan_schedule_init(void);
void fan_schedule_set_config(uint32_t interval_min, uint32_t run_min, bool enabled);
void fan_schedule_get_config(uint32_t *interval_min, uint32_t *run_min, bool *enabled);

// Called every AQ_IDLETIME from task_airquality to drive the state machine.
void fan_schedule_tick(void);

#ifdef __cplusplus
}
#endif
