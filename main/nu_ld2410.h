/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
// #include "driver/adc.h"
// #include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#ifdef __cplusplus
extern "C"
{
#endif

#define NU_MODEL_STR "Feedforward Neural Network"
#define NU_NON_OCCUPANCY_TIMES 180

#define SENSOR_SIZE (18 + 2) /* LD2410 16, Temp 1, Humidity 1 */
#define TIME_WINDOW 1
#define NEAR_DOOR 0
// #define INPUT_SIZE ((SENSOR_SIZE * TIME_WINDOW) + NEAR_DOOR)
#define INPUT_SIZE SENSOR_SIZE
#define HIDDEN_SIZE 16
#define OUTPUT_SIZE 1
#define LEARNING_RATE 0.15f
#define HISTORY_SIZE (8)

#define W_IH_PATH "/spiffs/w_ih.bin"
#define W_HO_PATH "/spiffs/w_ho.bin"
#define B_H_PATH "/spiffs/b_h.bin"
#define B_O_PATH "/spiffs/b_o.bin"
#define P_T_PATH "/spiffs/p_t.bin"
#define H_I_PATH "/spiffs/h_i.bin"
#define S_T_PATH "/spiffs/s_t.bin"

    extern SemaphoreHandle_t gsemaNULD2410Cfg;

    int nu_ld2410_getPred(float *);
    int nu_ld2410_setPred(float);
    void nu_ld2410_resetbuffer(void);
    void nu_ld2410_resetweight(void);
    bool nu_ld2410_isnew(void);
    bool nu_ld2410_saveweights(void);
    bool nu_ld2410_restoreweights(void);
    void nu_ld2410_push_sensor_data(float *new_data);
    bool nu_ld2410_update(float *sensor_data, int human_present, int mode);
    float nu_ld2410_forward(float *input_data);
    void nu_ld2410_cal_still_rate(void);
    void nu_ld2410_save_still_threshold(void);
    void nu_ld2410_init_weights();
    void nu_ld2410_build_input_from_buffer(bool istraining);
    void nu_ld2410_build_input_from_history(int human_present);
    void show_to_history(void);
    void show_to_w_ih(void);
    void analyze_hidden_node_contributions();

    /* NU Weights Upload/Download */
    bool nu_ld2410_get_weights_base64(char *w_ih_b64, char *w_ho_b64,
                                      char *b_h_b64, char *b_o_b64);
    bool nu_ld2410_set_weights_from_base64(const char *w_ih_b64,
                                           const char *w_ho_b64,
                                           const char *b_h_b64,
                                           const char *b_o_b64);

#ifdef __cplusplus
}
#endif