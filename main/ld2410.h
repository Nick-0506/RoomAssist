/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defined Auto Learning Mode */
#define LD2410_AUTOLEARN_NU

#define TO_LD2410_GPIO_NUM      17
#define FROM_LD2410_GPIO_NUM    16

/* User Configure LD2410 */
#define SW_JUDGE_NOTEXIST           1
#define LD2410_CFG_MAXACTDIS        8       /* 6x0.75=4.5m */
#define LD2410_CFG_MAXSTADIS        8       /* 6x0.75=4.5m */
#define LD2410_CFG_MAXSENSITIVITY   100     
#define LD2410_CFG_MINSENSITIVITY   0       
#define LD2410_CFG_MAXIDEL          0       /* 0 sec */
#define LD2410_CFG_MINTIGGERDIS     80      /* 80cm */
#define LD2410_CFG_MINTIGGERCNT     1       /* 1times */
#if defined(SW_JUDGE_NOTEXIST)
#define LD2410_SW_CFG_IDELTIME      100       /* ms */
#define LD2410_SW_CFG_DETECTTIME    105       /* ms */
#define LD2410_SW_NOTEXIST_TIME     (5*1000)  /* ms */
#define LD2410_SW_NON_OCCUPANCY_DELAY  60     /* seconds */
#elif defined(HW_JUDGE_NOTEXIST)
#define LD2410_SW_CFG_IDELTIME      1000    /* 1000 ms */
#define LD2410_SW_CFG_DETECTTIME    2000    /* 2000 ms */
#define LD2410_SW_NOTEXIST_TIME     5000    /* 5000 ms */
#define LD2410_SW_NOEXIST_THRESHOLD ((LD2410_SW_NOTEXIST_TIME*10)/(LD2410_SW_CFG_IDELTIME+LD2410_SW_CFG_DETECTTIME))
#else
# !!! Error: Please define SW_JUDGE_NOTEXIST or HW_JUDGE_NOTEXIST !!!
#endif

/* UART */
#define PATTERN_CHR_NUM    (3)

/* LD2410 */
#define LD2410_CMD_START_VALUE      0xFF
#define LD2410_CMD_END_VALUE        0xFE
#define LD2410_CMD_SETDIS_VALUE     0xAA
#define LD2410_CMD_GETPARA_VALUE    0x61
#define LD2410_CMD_SETENG_VALUE     0x62
#define LD2410_CMD_SETGEN_VALUE     0x63
#define LD2410_CMD_SETIDL_VALUE     0x60
#define LD2410_CMD_SETSENS_VALUE    0x64

#define LD2410_INTERNALDELAY        500
#define LD2410_StartCommandLen          14
#define LD2410_EndCommandLen            12
#define LD2410_DisRateCommandLen        14
#define LD2410_ReadParaCommandLen       12
#define LD2410_ReplyTypeCommandLen      12
#define LD2410_SetDisCommandLen         14
#define LD2410_MaxDisIdelCommandLen     30

#define LD2410_OPT_DISRATE075           0x00
#define LD2410_OPT_DISRATE020           0x01

#define LD2410_RPLY_TYPE_OFFSET         6
#define LD2410_RPLY_STAUS_OFFSET        8
#define LD2410_RPLY_STAUS_LEN           1
#define LD2410_RPLY_ACTDIS_OFFSET       (LD2410_RPLY_STAUS_OFFSET+LD2410_RPLY_STAUS_LEN)
#define LD2410_RPLY_DIS_LEN             2
#define LD2410_RPLY_ACTPWR_OFFSET       (LD2410_RPLY_ACTDIS_OFFSET+LD2410_RPLY_DIS_LEN)
#define LD2410_RPLY_PWR_LEN             1
#define LD2410_RPLY_ENG_LEN             1
#define LD2410_RPLY_STADIS_OFFSET       (LD2410_RPLY_ACTPWR_OFFSET+LD2410_RPLY_PWR_LEN)
#define LD2410_RPLY_STAPWR_OFFSET       (LD2410_RPLY_STADIS_OFFSET+LD2410_RPLY_DIS_LEN)
#define LD2410_RPLY_DIS_OFFSET          (LD2410_RPLY_STAPWR_OFFSET+LD2410_RPLY_PWR_LEN)
#define LD2410_RPLY_MAXACTDIS_OFFSET    (LD2410_RPLY_DIS_OFFSET+LD2410_RPLY_DIS_LEN)
#define LD2410_DBG_FLAG_SOMEBODY        0x01
#define LD2410_DBG_FLAG_NOBODY          0x02
#define LD2410_RPLY_TYPE_ENG            0x01
#define LD2410_RPLY_TYPE_GEN            0x02
#define LD2410_MIN_DIS                  0
#define LD2410_MAX_DIS                  8
#define LD2410_ACT_EXIST                0x01
#define LD2410_STA_EXIST                0x02
#define LD2410_EXIST                    (LD2410_ACT_EXIST|LD2410_STA_EXIST)
#define LD2410_AUTOLEARNING_TOR         2

#define LD2410_NVS_NAMESPACE     "LD2410CFG"
#define LD2410_NVS_LEARNSTATUS_KEY  "learned"

#define LD2410_AN_TYPE_NONE             0x00
#define LD2410_AN_TYPE_SOMEONE          0x01
#define LD2410_AN_TYPE_NOONE            0x02
#define LD2410_AN_TYPE_STILLNESS        0x04

/* LD2410 commands */
bool ld2410_isOccupancyStatus();
int ld2410_setOccupancyStatus(bool );
int ld2410_getANType(char *);
int ld2410_setANType(char );
int ld2410_getLeaveDelayTime(uint32_t *);
int ld2410_setLeaveDelayTime(uint32_t );
int ld2410_getDebuggingMode(int *flag);
int ld2410_setDebuggingMode(int flag);
void ld2410_saveconfig(char *key, uint32_t data);
void ld2410_save_maxcounter(void);
void ld2410_save_maxpower(void);
void ld2410_setend(int );
void ld2410_setmaxdisidel(uint32_t maxactdis, uint32_t maxstadis, uint32_t maxidel);
void ld2410_setreplyeng(void);
void ld2410_setreplygen(void);
void ld2410_setSensitivity(uint32_t , uint32_t , uint32_t );
void ld2410_setstart(int );
void ld2410_uart_init(void);
void ld2410_updatestatus(uint8_t *data, int length);
void task_ld2410(void *pvParameter);
void task_uart_event(void *pvParameters);
void non_occupancy_delay_timer_callback();
#ifdef __cplusplus
}
#endif