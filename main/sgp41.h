/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        float temperature_c; /* Degrees Celsius */
        float humidity_rh;   /* Relative humidity percentage */
    } sgp41_environment_t;

    typedef struct
    {
        uint16_t voc_ticks;
        uint16_t nox_ticks;
        int voc_index; /* 0-500 heuristic air quality score */
        int nox_index; /* 0-500 heuristic combustion score */
    } sgp41_measurement_t;

    esp_err_t sgp41_init(void);
    esp_err_t sgp41_measure(const sgp41_environment_t *env,
                            sgp41_measurement_t *result, int gating_voc_high,
                            int gating_voc_low, int gating_nox_high,
                            int gating_nox_low);
    void sgp41_reset_baseline(void);

#ifdef __cplusplus
}
#endif
