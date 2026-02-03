/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZERO_FAN_DEFAULT_STATUS           0
#define ZERO_FAN_DEFAULT_SPEED            1
#define ZERO_FAN_DEFAULT_SWING            0

#define ZEROFAN_NVS_NAMESPACE    "ZEROFANCFG"
#define ZEROFAN_NVS_STATUS_KEY   "status"
#define ZEROFAN_NVS_SPEED_KEY    "speed"
#define ZEROFAN_NVS_SWING_KEY    "swing"

extern SemaphoreHandle_t gsemaIRZEROCfg;

/**
 * @brief IR +-0 scan code representation
 */
typedef struct {
   int repeat;
    char type;
    int targetfreq;
    int pwrthreshold;        
    uint8_t time[4];    
    uint8_t data[2];
} ir_zro_scan_code_t;

/**
 * @brief Type of IR +-0 encoder configuration
 */
typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} ir_zro_encoder_config_t;

/**
 * @brief Create RMT encoder for encoding IR HITACHI frame into RMT symbols
 *
 * @param[in] config Encoder configuration
 * @param[out] ret_encoder Returned encoder handle
 * @return
 *      - ESP_ERR_INVALID_ARG for any invalid arguments
 *      - ESP_ERR_NO_MEM out of memory when creating IR NEC encoder
 *      - ESP_OK if creating encoder successfully
 */
esp_err_t rmt_new_ir_zro_encoder(const ir_zro_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);

bool rmt_iszerofanactive();
int rmt_setzerofanstatus(bool );
int rmt_getzerofanspeed(int *);
int rmt_setzerofanspeed(int );
int rmt_getzerofanswing(int *);
int rmt_setzerofanswing(int );
void ir_zerofan_restoreconfig(void);

#ifdef __cplusplus
}
#endif