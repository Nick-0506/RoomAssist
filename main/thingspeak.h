/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THINGSPEAK_NVS_NAMESPACE      "TINGSPK"
#define THINGSPEAK_NVS_API_KEY        "APIKEY"
#define THINGSPEAK_API_KEYLENGTH      16

void task_thingspeak(void *arg);
void thingspeak_saveconfig(void);
int thingspeak_getapikey(char *, int );
int thingspeak_setapikey(char *, int );

#ifdef __cplusplus
}
#endif