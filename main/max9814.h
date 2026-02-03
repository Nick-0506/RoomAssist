/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX9814_BEE_FREQ_RANGE 5
#define MAX9814_HITACHI_AC_BEE_FREQ 3900
#define MAX9814_ZERO_FAN_BEE_FREQ 3850
#define MAX9814_DELTA_FAN_BEE_FREQ 2380

#define MAX9814_HITACHI_AC_BEE_THRESHOLD 100000
#define MAX9814_ZERO_FAN_BEE_THRESHOLD 200000
#define MAX9814_DELTA_FAN_BEE_THRESHOLD 300000
    // #define FFT4REAL

    void max9814_sample_adc();
    void max9814_sample_adc4real();
    void max9814_init_fft();
    void max9814_init_fft4real();
    void max9814_compute_fft();
    void max9814_compute_fft4real();
    float max9814_find_frequency_of_maxpwr();
    float max9814_find_sumpwr_of_frequency(int, int);
    void max9814_setup();
    bool max9814_buildup();
    bool max9814_buildup4real();
    void max9814_teardown();
    void max9814_check_bee(int checkfreq, int check2ndfreq, int *pwr1st,
                           int *pwr2nd);
    void max9814_display_signal(float *, int maxnumer, int);

#ifdef __cplusplus
}
#endif
