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

#define DELTA_FAN_DEFAULT_STATUS          false
#define DELTA_FAN_DEFAULT_SPEED           1
#define DELTA_FAN_DEFAULT_SWING           0

#define DELTA_FAN_MODE_EXHAUST_MAX      0x19
#define DELTA_FAN_MODE_EXHAUST_MIN      0x00 //TODO
#define DELTA_FAN_MODE_DRY_MAX          0x15
#define DELTA_FAN_MODE_DRY_MIN          0x00 //TODO
#define DELTA_FAN_MODE_HEATER_MAX       0x18
#define DELTA_FAN_MODE_HEATER_MIN       0x00 //TODO
#define DELTA_FAN_MODE_COOLER_MAX       0x00 //TODO
#define DELTA_FAN_MODE_COOLER_MIN       0x00 //TODO
#define DELTA_FAN_MODE_OFF              0x12

#define DELTA_FAN_DURATION_HALF_HOUR    0x01
#define DELTA_FAN_DURATION_ONE_HOUR     0x02
#define DELTA_FAN_DURATION_TWO_HOUR     0x03
#define DELTA_FAN_DURATION_THREE_HOUR   0x04
#define DELTA_FAN_DURATION_FOUR_HOUR    0x05
#define DELTA_FAN_DURATION_SIX_HOUR     0x06
#define DELTA_FAN_DURATION_FOREVER      0x07
extern SemaphoreHandle_t gsemaIRDELTACfg;

/**
 * @brief IR delta scan code representation
 */
typedef struct {
    int repeat;
    char type;
    int targetfreq;
    int pwrthreshold;    
    uint8_t time[4];    
    uint8_t data[4];
} ir_delta_scan_code_t;

/**
 * @brief Type of IR delta encoder configuration
 */
typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} ir_delta_encoder_config_t;

/**
 * @brief Create RMT encoder for encoding IR delta frame into RMT symbols
 *
 * @param[in] config Encoder configuration
 * @param[out] ret_encoder Returned encoder handle
 * @return
 *      - ESP_ERR_INVALID_ARG for any invalid arguments
 *      - ESP_ERR_NO_MEM out of memory when creating IR NEC encoder
 *      - ESP_OK if creating encoder successfully
 */
esp_err_t rmt_new_ir_delta_encoder(const ir_delta_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);
bool rmt_ismanualfanactive();
bool rmt_isdryfanactive();
bool rmt_iswarmfanactive();
bool rmt_isexhaustfanactive();
int rmt_setmanualfanstatus(bool );
int rmt_setdryfanstatus(bool );
int rmt_setwarmfanstatus(bool );
int rmt_setexhaustfanstatus(bool );
void ir_deltafan_restoreconfig(void);

#ifdef __cplusplus
}
#endif