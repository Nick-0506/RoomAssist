/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "dht.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

// DHT22 configuration 
#define DHT22_SW_CFG_IDELTIME      (60*1000)
#define DHT22_SW_CFG_HISTORYTIME   (5*60*1000)
#define DHT22_GPIO_NUM          21

#define DHT22_NVS_NAMESPACE      "DHT22"
#define DHT22_NVS_TEMP_THRESHOLD_HIGH_KEY  "Temp-TH-high"
#define DHT22_NVS_TEMP_THRESHOLD_LOW_KEY  "Temp-TH-low"
#define DHT22_NVS_HUMI_THRESHOLD_HIGH_KEY  "Humi-TH-high"
#define DHT22_NVS_HUMI_THRESHOLD_LOW_KEY  "Humi-TH-low"

// Default Value
#define DHT22_DEFAULT_TEMP_HI_THOLD     24
#define DHT22_DEFAULT_TEMP_LO_THOLD     24
#define DHT22_DEFAULT_HUMI_HI_THOLD     80
#define DHT22_DEFAULT_HUMI_LO_THOLD     70

#define DHT22_MAX_ERROR_TIMES           3

extern esp_timer_handle_t gsync_delta_warm_timer_handle;

void task_dht22(void *arg);
void dht22_saveconfig(char *key, int32_t data);
void ir_sync_delta_dry_timer_callback();
void ir_sync_delta_warm_timer_callback();
int dht22_getcurrenttemperature(float *);
int dht22_setcurrenttemperature(float );
int dht22_getcurrenthumidity(float *);
int dht22_setcurrenthumidity(float );

int dht22_gethightemperature(int *);
int dht22_getlowtemperature(int *);
int dht22_sethightemperature(int );
int dht22_setlowtemperature(int );
int dht22_gethighhumidity(int *);
int dht22_getlowhumidity(int *);
int dht22_sethighhumidity(int );
int dht22_setlowhumidity(int );

#ifdef __cplusplus
}
#endif