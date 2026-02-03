/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AC_DEFAULT_STATUS                0
#define AC_DEFAULT_TYPE                  0  /* Auto */
#define AC_DEFAULT_TEMP                  26
#define AC_DEFAULT_SPEED                 5
#define AC_DEFAULT_SWING                 0

#define HITACHI_AC_MODE_AUTO             7
#define HITACHI_AC_MODE_HEATER           6
#define HITACHI_AC_MODE_COOLER           3
#define HITACHI_AC_MODE_FAN_ONLY         1

#define HITACHI_AC_SLI_FAN_SPEED         1
#define HITACHI_AC_MIN_FAN_SPEED         2
#define HITACHI_AC_MID_FAN_SPEED         3
#define HITACHI_AC_MAX_FAN_SPEED         4
#define HITACHI_AC_AUTO_FAN_SPEED        5

#define AC_NVS_NAMESPACE        "ACCFG"
#define AC_NVS_STATUS_KEY       "status"
#define AC_NVS_TYPE_KEY         "type"
#define AC_NVS_TEMP_KEY         "temp"
#define AC_NVS_SPEED_KEY        "speed"
#define AC_NVS_SWING_KEY        "swing"

extern SemaphoreHandle_t gsemaIRACCfg;
/**
 * @brief IR HITACHI scan code representation
 */
typedef struct {
    int repeat;
    char type;
    int targetfreq;
    int pwrthreshold;    
    uint8_t time[4];    
    uint8_t data[44];
} ir_hta_scan_code_t;

/**
 * @brief Type of IR NEC encoder configuration
 */
typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} ir_hta_encoder_config_t;

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
esp_err_t rmt_new_ir_hta_encoder(const ir_hta_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);
bool rmt_isiracactive();
int rmt_setiracstatus(bool );
int rmt_getiracmode(int *);
int rmt_setiracmode(int );
int rmt_getiractemp(int *);
int rmt_setiractemp(int );
int rmt_getiracspeed(int *);
int rmt_setiracspeed(int );
int rmt_getiracswing(int *);
int rmt_setiracswing(int );
void ir_ac_restoreconfig(void);

#ifdef __cplusplus
}
#endif