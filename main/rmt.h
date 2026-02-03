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
/* For RMT */
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "ir_hta_encoder.h"
#include "ir_zro_encoder.h"
#include "ir_delta_encoder.h"
#include "ir_dyson_encoder.h"
#include "homekit.h"

#ifdef __cplusplus
extern "C" {
#endif

// RMT constance
#define IR_RESOLUTION_HZ     1000000 // 38kHz resolution, 1 tick = 1us
#define RMT_TX_GPIO_NUM          18
#define RMT_RX_GPIO_NUM          19
#define RMT_CHECK_TIMES          3
#define RMT_RETRY_TIMES          3
#define RMT_FREQ_THRESHOLD       200000
#define RMT_FREQ_GAP             50000
#define RMT_ISNOT_HEAR ((beepwr[checkbee-1]>0) && (beepwr[checkbee-1]<rmt_msg.pwrthreshold))
#define IR_TYPE_HITACHI  1
#define IR_TYPE_ZERO     2
#define IR_TYPE_DELTA    3
#define IR_TYPE_DYSON    4

/* Define Hitachi IR Protocol */
#define HITACHI_IRP_OPCODE_BYTE         11
#define HITACHI_IRP_OPCODE_ACTIVE_VALUE 0x13
#define HITACHI_IRP_OPCODE_STATE_VALUE  0x41
#define HITACHI_IRP_OPCODE_SPEED_VALUE  0x42
#define HITACHI_IRP_OPCODE_TEMP_VALUE   0x43
#define HITACHI_IRP_OPCODE_SWING_VALUE  0x81

#define HITACHI_IRP_ACTICE_BYTE         27
#define HITACHI_IRP_STATE_BYTE          25

#define HITACHI_IRP_SWING_RL_BYTE       35
#define HITACHI_IRP_SWING_RL_BIT        2
#define HITACHI_IRP_SWING_UD_BYTE       37
#define HITACHI_IRP_SWING_UD_BIT        5
#define HITACHI_IRP_SWING_VALUE         0x20
#define HITACHI_IRP_NOSWING_VALUE       0 
#define HITACHI_IRP_STATE_TYPE_MASK     0x0F
#define HITACHI_IRP_STATE_FAN_MASK      0xF0
#define HITACHI_IRP_TEMPERATURE_BYTE_HI 13
#define HITACHI_IRP_TEMPERATURE_BYTE_LO 14
#define HITACHI_IRP_ACTIVE_VALUE        0xF1
#define HITACHI_IRP_INACTIVE_VALUE      0xE1
#define HITACHI_IRP_COOLER_VALUE        0x03
#define HITACHI_IRP_HEATER_VALUE        0x06
#define HITACHI_IRP_HUM_VALUE           0x05
#define HITACHI_IRP_FAN_VALUE           0x01
#define HITACHI_IRP_AUTO_VALUE          0x07
#define HITACHI_IRP_FANHI_VALUE         0x40
#define HITACHI_IRP_FANMD_VALUE         0x30
#define HITACHI_IRP_FANLO_VALUE         0x20
#define HITACHI_IRP_FANEC_VALUE         0x10
#define HITACHI_IRP_FANAU_VALUE         0x50
#define HITACHI_MIN_TEMPERATURE         16
#define HITACHI_MIN_TEMPERATURE_HI      0x40
#define HITACHI_MIN_TEMPERATURE_LO      0xBF
#define HITACHI_MIN_TEMPERATURE_HILO    0x40BF
#define HITACHI_DEF_TEMPERATURE         25
#define HITACHI_DEF_TEMPERATURE_HI      0x64
#define HITACHI_DEF_TEMPERATURE_LO      0x9B
#define HITACHI_DEF_TEMPERATURE_HILO    0x649B
#define HITACHI_GAP_TEMPERATURE         1020

/**
 * @brief HITACH timing spec
 */
#define HTA_LEADING_CODE_DURATION_0     3400
#define HTA_LEADING_CODE_DURATION_1     1600
#define HTA_PAYLOAD_ZERO_DURATION_0     420
#define HTA_PAYLOAD_ZERO_DURATION_1     420
#define HTA_PAYLOAD_ONE_DURATION_0      420
#define HTA_PAYLOAD_ONE_DURATION_1      1300
#define HTA_REPEAT_CODE_DURATION_0      3400
#define HTA_REPEAT_CODE_DURATION_1      1600
#define HTA_END_CODE_DURATION_0         450
#define HTA_END_CODE_DURATION_1         0
#define IR_HTA_DECODE_MARGIN            400     // Tolerance for parsing RMT symbols into bit stream

/**
 * @brief +-0 timing spec
 */
#define ZERO_LEADING_CODE_DURATION_0     2500
#define ZERO_LEADING_CODE_DURATION_1     3300
#define ZERO_PAYLOAD_ZERO_DURATION_0     450
#define ZERO_PAYLOAD_ZERO_DURATION_1     1200
#define ZERO_PAYLOAD_ONE_DURATION_0      1300
#define ZERO_PAYLOAD_ONE_DURATION_1      400
#define ZERO_REPEAT_CODE_DURATION_0      450
#define ZERO_REPEAT_CODE_DURATION_1      6600
#define ZERO_END_CODE_DURATION_0         450
#define ZERO_END_CODE_DURATION_1         0
#define IR_ZERO_DECODE_MARGIN            400     // Tolerance for parsing RMT symbols into bit stream


/**
 * @brief Delta timing spec
 */
#define DELTA_LEADING_CODE_DURATION_0     9000
#define DELTA_LEADING_CODE_DURATION_1     4450
#define DELTA_PAYLOAD_ZERO_DURATION_0     590
#define DELTA_PAYLOAD_ZERO_DURATION_1     550
#define DELTA_PAYLOAD_ONE_DURATION_0      590
#define DELTA_PAYLOAD_ONE_DURATION_1      1640
#define DELTA_END_CODE_DURATION_0         590
#define DELTA_END_CODE_DURATION_1         0
#define DELTA_REPEAT_CODE_DURATION_0      590
#define DELTA_REPEAT_CODE_DURATION_1      19910
#define IR_DELTA_DECODE_MARGIN            20     // Tolerance for parsing RMT symbols into bit stream

/**
 * @brief Dyson timing spec
 */
#define DYSON_LEADING_CODE_DURATION_0     2250
#define DYSON_LEADING_CODE_DURATION_1     650
#define DYSON_PAYLOAD_ZERO_DURATION_0     800
#define DYSON_PAYLOAD_ZERO_DURATION_1     650
#define DYSON_PAYLOAD_ONE_DURATION_0      800
#define DYSON_PAYLOAD_ONE_DURATION_1      1300
#define DYSON_END_CODE_DURATION_0         800
#define DYSON_END_CODE_DURATION_1         0
#define IR_DYSON_DECODE_MARGIN            100     // Tolerance for parsing RMT symbols into bit stream

#define RMT_RX_MEM_BLK_SYMB 384
#define RMT_TX_MEM_BLK_SYMB 128

#define IR_DELTA_FAN_TIGGER_ACTIVE_ON       1
#define IR_DELTA_FAN_TIGGER_ACTIVE_OFF      2

/* Defined based on priority */
#define IR_DELTA_FAN_TIGGER_MODE_MANUAL   0
#define IR_DELTA_FAN_TIGGER_MODE_EXHAUST  1
#define IR_DELTA_FAN_TIGGER_MODE_WARM     2
#define IR_DELTA_FAN_TIGGER_MODE_DRY      3
#define IR_DELTA_FAN_TIGGER_MODE_HOMEKIT  4
#define IR_DELTA_FAN_TIGGER_MODE_OFF      5
#define IR_DELTA_FAN_TIGGER_MODE_MAX      6

#define IR_DELTA_FAN_DURATION_HALF_HOUR   0
#define IR_DELTA_FAN_DURATION_1HR         1
#define IR_DELTA_FAN_DURATION_2HR         2
#define IR_DELTA_FAN_DURATION_3HR         3
#define IR_DELTA_FAN_DURATION_4HR         4
#define IR_DELTA_FAN_DURATION_6HR         6
#define IR_DELTA_FAN_DURATION_FOREVER     9

#define DELTA_FAN_SCHDULER_IDEL     0
#define DELTA_FAN_SCHDULER_PENDING  1
#define DELTA_FAN_SCHDULER_ACTIVE   2

typedef struct {
    int repeat;
    char type;
    int targetfreq;
    int pwrthreshold;
    uint8_t time[4];
    uint8_t data[44];
    bool bstatusch;
    bool bmodech;
    bool bfanspeedch;
    bool bswingch;
    bool bhithch;
    bool blothch;
    bool bdurationch;
    bool status;
    uint8_t mode;
    int fanspeed;
    int swing;
    int hith;
    int loth;
    int duration;
} rmt_msg_t;

typedef struct {
    bool bmodech;       /* Cold, Warm, Auto */
    bool bactivech;     /* On, Off */
    bool blowtempch;    
    bool bhightempch;
    bool bfanspeedch;
    bool bswingch;
    int mode;    
    int active;
    int lowtemp;
    int hightemp;
    int fanspeed;
    int swing;
} rmt_hattg_msg_t;

typedef struct {
    bool bactivech;     /* On, Off */
    bool bfanspeedch;
    bool bswingch;
    int active;    
    int fanspeed;
    int swing;
} rmt_zftg_msg_t;

extern QueueHandle_t gqueue_rmt_tx;

void task_rmt(void *pvParameters);
int ir_deltafan_tigger(int mode, int active, int duration);
int ir_hitachiac_tigger(rmt_hattg_msg_t msg);
int ir_zerofan_tigger(rmt_zftg_msg_t msg);
void ir_hitachiac_delay_timer_callback();
void ir_sync_delta_manual_timer_callback();
int ir_get_deltascheduler(int, uint8_t *);
int ir_set_deltascheduler(int, uint8_t);
int ir_set_hitachi_config(uint8_t config);
int ir_get_hitachi_config(uint8_t *config);
int rmt_form_tx_data(rmt_msg_t *rmt_msg);
#ifdef __cplusplus
}
#endif
