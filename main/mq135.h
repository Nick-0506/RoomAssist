/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdio.h>
#include <math.h>
#include "esp_err.h"

// Configuration Constants
#define MQ135_ADC_UNIT ADC_UNIT_1
#define MQ135_ADC_CHANNEL ADC_CHANNEL_3
#define MQ135_ADC_ATTEN ADC_ATTEN_DB_12
#define MQ135_ADC_BITWIDTH ADC_BITWIDTH_DEFAULT
#define MQ135_ADC_FULL_SCALE_MV 3300
#define MQ135_ADC_MAX_READING 4095

#define MQ135_SW_BASE_VOLTAGE 500  // Base voltage for clean air (mv)

// API Access
esp_err_t mq135_adc_init(void);
esp_err_t mq135_read_aqi(int *aqi);

// Helper for voltage reading (optional debug)
esp_err_t mq135_read_voltage(uint32_t *voltage_mv);
