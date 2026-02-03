/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "rmt.h"
#include "max9814.h"
#include "system.h"
#include "homekit.h"
#include "ld2410.h"
#include "elf.h"
#include "syslog.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* Hitachi IR Protocol default data */
uint8_t grmt_htaBuffer[44] = {
    0x01, 0x10, 0x00, 0x40, 0xBF, 0xFF, 0x00, 0xCC, 0x33, 0x98, 0x67,
    0x13, 0xEC, 0x64, 0x9B, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
    0xFF, 0x00, 0xFF, 0x13, 0xEC, 0xF1, 0x0E, 0x18, 0xE7, 0x06, 0xF9,
    0x80, 0x7F, 0x03, 0xFC, 0x20, 0xDF, 0x05, 0xFA, 0x51, 0xAE, 0x00};

/* +-0 Fan IR Protocol default data */
uint8_t grmt_zroBuffer[2] = {0x76, 0x80};

/* Delta Fan IR Ptorovol detault data */
uint8_t grmt_deltaBuffer[4] = {0x00, 0x0F, 0x12, 0xED};
uint8_t grmt_deltatimerBuffer[4] = {0x00, 0x0F, 0x05,
                                    0xFA}; /* Enable Delta Fan 4HR */
uint8_t grmt_deltascheduler[IR_DELTA_FAN_TIGGER_MODE_MAX] = {
    DELTA_FAN_SCHDULER_IDEL, DELTA_FAN_SCHDULER_IDEL, DELTA_FAN_SCHDULER_IDEL,
    DELTA_FAN_SCHDULER_IDEL, DELTA_FAN_SCHDULER_IDEL};
uint8_t grmt_deltaschedulerName[IR_DELTA_FAN_TIGGER_MODE_MAX + 1][10] = {
    "Manual", "Exhaust", "Warm", "Dry", "Homekit", "Off", "Keep"};
QueueHandle_t gqueue_rmt_tx;
ir_hta_scan_code_t grmt_hta_data;
ir_zro_scan_code_t grmt_zro_data;
ir_delta_scan_code_t grmt_delta_data;
ir_dyson_scan_code_t grmt_dyson_data;
esp_timer_handle_t ghitachiac_delay_timer_handle = NULL;
esp_timer_handle_t gsync_delta_manual_timer_handle = NULL;

static void ir_update_hap_Hitachi_status(int active, int temp, int state,
                                         int speed, int swing);
static void ir_update_hap_zerofan_status(int active, int speed, int swing);

static bool rmt_enqueue_msg(const rmt_msg_t *msg)
{
    if (gqueue_rmt_tx == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_ERROR,
                       "TX queue not ready");
        return false;
    }
    if (xQueueSend(gqueue_rmt_tx, msg, 0) != pdTRUE)
    {
        syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_ERROR,
                       "TX queue full");
        return false;
    }
    return true;
}

static inline void rmt_rx_gpio_disable(void)
{
    gpio_set_direction(RMT_RX_GPIO_NUM, GPIO_MODE_DISABLE);
}

static inline void rmt_rx_gpio_enable(void)
{
    gpio_set_direction(RMT_RX_GPIO_NUM, GPIO_MODE_INPUT);
    gpio_set_pull_mode(RMT_RX_GPIO_NUM, GPIO_FLOATING);
}

SemaphoreHandle_t gsemaRMTCfg = NULL;

static void dbg_ir_tx_rmt_dataraw(uint8_t *data, int length);
static inline bool ir_check_in_range(uint32_t signal_duration,
                                     uint32_t spec_duration);
static bool ir_parse_frame_end(rmt_symbol_word_t *rmt_nec_symbols, char type);
static bool ir_parse_frame_repeat(rmt_symbol_word_t *rmt_nec_symbols,
                                  char type);
static char ir_parse_frame(rmt_symbol_word_t *rmt_nec_symbols, int framelen);
static void ir_parse_ir_frame(rmt_symbol_word_t *rmt_nec_symbols,
                              size_t symbol_num);
static bool ir_parse_logic0(rmt_symbol_word_t *rmt_nec_symbols, char type);
static bool ir_parse_logic1(rmt_symbol_word_t *rmt_nec_symbols, char type);
static bool ir_rmt_rx_done_callback(rmt_channel_handle_t channel,
                                    const rmt_rx_done_event_data_t *edata,
                                    void *user_data);
static bool ir_rmt_tx_done_callback(rmt_channel_handle_t channel,
                                    const rmt_tx_done_event_data_t *edata,
                                    void *user_data);
static void ir_update_Hitachi_status(int active, int temp, int state, int speed,
                                     int swing);
static void ir_update_zerofan_status(int active, int speed, int swing);
static void ir_update_deltafan_status(int active, int speed);
static void rmt_restart_receive(rmt_channel_handle_t channel,
                                rmt_symbol_word_t *symbols, size_t symbols_size,
                                const rmt_receive_config_t *config);

static void dbg_ir_tx_rmt_dataraw(uint8_t *data, int length)
{
    dbg_printf("\n\n DBG Tx RMT data:\n");
    for (int i = 0; i < length; i++)
    {
        if (i % 4 == 0) dbg_printf(" ");
        dbg_printf("%02X", data[i]);
    }
    dbg_printf("\n -------- end -------\n\n");
}

/**
 * @brief Check whether a duration is within expected range
 */
static inline bool ir_check_in_range(uint32_t signal_duration,
                                     uint32_t spec_duration)
{
    return (signal_duration < (spec_duration + IR_HTA_DECODE_MARGIN)) &&
           (signal_duration > ((spec_duration > IR_HTA_DECODE_MARGIN)
                                   ? (spec_duration - IR_HTA_DECODE_MARGIN)
                                   : 0));
}

/**
 * @brief Check whether the RMT symbols represent NEC repeat code
 */
static bool ir_parse_frame_end(rmt_symbol_word_t *rmt_nec_symbols, char type)
{
    int end_dur_0 = 0;
    int end_dur_1 = 0;
    switch (type)
    {
        case IR_TYPE_HITACHI:
            end_dur_0 = HTA_END_CODE_DURATION_0;
            end_dur_1 = HTA_END_CODE_DURATION_1;
            break;
        case IR_TYPE_ZERO:
            end_dur_0 = ZERO_END_CODE_DURATION_0;
            end_dur_1 = ZERO_END_CODE_DURATION_1;
            break;
        case IR_TYPE_DELTA:
            end_dur_0 = DELTA_END_CODE_DURATION_0;
            end_dur_1 = DELTA_END_CODE_DURATION_1;
            break;
        case IR_TYPE_DYSON:
            end_dur_0 = DYSON_END_CODE_DURATION_0;
            end_dur_1 = DYSON_END_CODE_DURATION_0;
            break;
        default:
            break;
    }

    return ((rmt_nec_symbols->duration0 < end_dur_0 + IR_HTA_DECODE_MARGIN) &&
            (rmt_nec_symbols->duration0 > end_dur_0 - IR_HTA_DECODE_MARGIN));
}

/**
 * @brief Check whether the RMT symbols represent NEC repeat code
 */
static bool ir_parse_frame_repeat(rmt_symbol_word_t *rmt_nec_symbols, char type)
{
    int rpt_dur_0 = 0;
    int rpt_dur_1 = 0;
    switch (type)
    {
        case IR_TYPE_HITACHI:
            rpt_dur_0 = 0;
            rpt_dur_1 = 0;
            break;
        case IR_TYPE_ZERO:
            rpt_dur_0 = ZERO_REPEAT_CODE_DURATION_0;
            rpt_dur_1 = ZERO_REPEAT_CODE_DURATION_1;
            break;
        case IR_TYPE_DELTA:
            rpt_dur_0 = DELTA_REPEAT_CODE_DURATION_0;
            rpt_dur_1 = DELTA_REPEAT_CODE_DURATION_1;
            break;
        default:
            break;
    }
    return ir_check_in_range(rmt_nec_symbols->duration0, rpt_dur_0) &&
           ir_check_in_range(rmt_nec_symbols->duration1, rpt_dur_1);
}

/**
 * @brief Decode RMT symbols into NEC address and command
 */
static char ir_parse_frame(rmt_symbol_word_t *rmt_nec_symbols, int framelen)
{
    int i = 0;
    int loc = 0;
    rmt_symbol_word_t *cur = rmt_nec_symbols;
    char parse_type = 0;
    bool valid_leading_code = false;
    int counter1 = 0, counter0 = 0;

    valid_leading_code =
        ir_check_in_range(cur->duration0, HTA_LEADING_CODE_DURATION_0) &&
        ir_check_in_range(cur->duration1, HTA_LEADING_CODE_DURATION_1);
    if (valid_leading_code)
    {
        parse_type = IR_TYPE_HITACHI;
    }

    valid_leading_code =
        ir_check_in_range(cur->duration0, ZERO_LEADING_CODE_DURATION_0) &&
        ir_check_in_range(cur->duration1, ZERO_LEADING_CODE_DURATION_1);
    if (valid_leading_code)
    {
        parse_type = IR_TYPE_ZERO;
    }

    valid_leading_code =
        ir_check_in_range(cur->duration0, DELTA_LEADING_CODE_DURATION_0) &&
        ir_check_in_range(cur->duration1, DELTA_LEADING_CODE_DURATION_1);
    if (valid_leading_code)
    {
        parse_type = IR_TYPE_DELTA;
    }

    valid_leading_code =
        ir_check_in_range(cur->duration0, DYSON_LEADING_CODE_DURATION_0) &&
        ir_check_in_range(cur->duration1, DYSON_LEADING_CODE_DURATION_1);
    if (valid_leading_code)
    {
        parse_type = IR_TYPE_DYSON;
    }

    if (parse_type == 0)
    {
        return false;
    }

    cur++;
    if (parse_type == IR_TYPE_HITACHI)
    {
        for (i = 0; i < framelen - 1; i++)
        {
            if (ir_parse_logic1(cur, parse_type))
            {
                grmt_hta_data.data[i / 8] |= 1 << (i % 8);
            }
            else if (ir_parse_logic0(cur, parse_type))
            {
                grmt_hta_data.data[i / 8] &= ~(1 << (i % 8));
            }
            else if (ir_parse_frame_end(cur, parse_type))
            {
                break;
            }
            else
            {
                return 0;
            }
            cur++;
        }
#if 0
        dbg_printf("\n HITACHI pass \n");

        for(i=0;i<44;i++)
        {
            if(i%4==0)
                dbg_printf(" ");
            dbg_printf("%02X",grmt_hta_data.data[i]);
        }
#endif
    }
    if (parse_type == IR_TYPE_ZERO)
    {
        for (i = 0; i < framelen - 1; i++)
        {
            if (ir_parse_logic1(cur, parse_type))
            {
                grmt_zro_data.data[loc / 8] |= 1 << (loc % 8);
                loc++;
            }
            else if (ir_parse_logic0(cur, parse_type))
            {
                grmt_zro_data.data[loc / 8] &= ~(1 << (loc % 8));
                loc++;
            }
            else if (ir_parse_frame_repeat(cur, parse_type))
            {
                grmt_zro_data.repeat++;
                loc = 0;
                cur++;
            }
            else if (ir_parse_frame_end(cur, parse_type))
            {
                break;
            }
            else
            {
                return 0;
            }
            cur++;
        }
#if 0
        dbg_printf("\n ZERO pass \n");

        for(i=0;i<2;i++)
        {
            if(i%4==0)
                dbg_printf(" ");
            dbg_printf("%02X",grmt_zro_data.data[i]);
        }
        printf("\n Repeat %d\n",grmt_zro_data.repeat);
#endif
    }
    if (parse_type == IR_TYPE_DELTA)
    {
        int repeat = 0;
        for (i = 0; i < framelen - 1; i++)
        {
            if (ir_parse_logic1(cur, parse_type))
            {
                if (repeat)
                {
                    grmt_delta_data.time[loc / 8] |= 1 << (loc % 8);
                }
                else
                {
                    grmt_delta_data.data[loc / 8] |= 1 << (loc % 8);
                }
                loc++;
                counter1++;
            }
            else if (ir_parse_logic0(cur, parse_type))
            {
                if (repeat)
                {
                    grmt_delta_data.time[loc / 8] &= ~(1 << (loc % 8));
                }
                else
                {
                    grmt_delta_data.data[loc / 8] &= ~(1 << (loc % 8));
                }
                loc++;
                counter0++;
            }
            else if (ir_parse_frame_repeat(cur, parse_type))
            {
                grmt_delta_data.repeat++;
                repeat = 1;
                loc = 0;
                cur++;
            }
            else if (ir_parse_frame_end(cur, parse_type))
            {
                break;
            }
            else
            {
                return 0;
            }
            cur++;
        }
#if 0
        dbg_printf("\n DELTA pass\n");
        
        for(i=0;i<4;i++)
        {
            if(i%4==0)
                dbg_printf(" ");
            dbg_printf("%02X",grmt_delta_data.data[i]);
        }
        for(i=0;i<4;i++)
        {
            if(i%4==0)
                dbg_printf(" ");
            dbg_printf("%02X",grmt_delta_data.time[i]);
        }
        dbg_printf("\n");
#endif
    }
    if (parse_type == IR_TYPE_DYSON)
    {
        for (i = 0; i < framelen - 1; i++)
        {
            if (ir_parse_logic1(cur, parse_type))
            {
                grmt_dyson_data.data[loc / 8] |= 1 << (loc % 8);
                loc++;
            }
            else if (ir_parse_logic0(cur, parse_type))
            {
                grmt_dyson_data.data[loc / 8] &= ~(1 << (loc % 8));
                loc++;
            }
            else if (ir_parse_frame_repeat(cur, parse_type))
            {
                grmt_dyson_data.repeat++;
                loc = 0;
                cur++;
            }
            else if (ir_parse_frame_end(cur, parse_type))
            {
                break;
            }
            else
            {
                return 0;
            }
            cur++;
        }
#if 0
        dbg_printf("\n DYSON pass \n");

        for(i=0;i<3;i++)
        {
            if(i%4==0)
                dbg_printf(" ");
            dbg_printf("%02X",grmt_dyson_data.data[i]);
        }
        printf("\n Repeat %d\n",grmt_dyson_data.repeat);
#endif
    }
    return parse_type;
}

/**
 * @brief Decode RMT symbols into NEC scan code and print the result
 */
static void ir_parse_ir_frame(rmt_symbol_word_t *rmt_nec_symbols,
                              size_t symbol_num)
{
    int active = 0;
    int temp = 0;
    int mode = 0;
    int speed = 0;
    int swing = -1;
    char irtype = 0;
    uint8_t sys_mac[6];

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));

    esp_timer_create_args_t sync_delta_manual_timer_args = {
        .callback = &ir_sync_delta_manual_timer_callback,
        .name = "sync_delta_timer"  // 計時器名稱，用於調試
    };

#if 0
    printf("\n\nIR frame start---\r\n");
    for (size_t i = 0; i < symbol_num; i++) {
        printf("{%d:%d},{%d:%d}\r\n", rmt_nec_symbols[i].level0, rmt_nec_symbols[i].duration0,
               rmt_nec_symbols[i].level1, rmt_nec_symbols[i].duration1);
    }
    printf("--- frame end: %d\n\n",symbol_num);
#endif
    // decode RMT symbols
    memset(&grmt_hta_data, 0, sizeof(ir_hta_scan_code_t));
    memset(&grmt_zro_data, 0, sizeof(ir_zro_scan_code_t));
    irtype = ir_parse_frame(rmt_nec_symbols, symbol_num);
    switch (irtype)
    {
        case IR_TYPE_HITACHI:
            if (!IS_BATHROOM(sys_mac))
            {
                temp = (((grmt_hta_data.data[HITACHI_IRP_TEMPERATURE_BYTE_HI]
                          << 8) +
                         grmt_hta_data.data[HITACHI_IRP_TEMPERATURE_BYTE_LO]) -
                        HITACHI_MIN_TEMPERATURE_HILO) /
                           HITACHI_GAP_TEMPERATURE +
                       HITACHI_MIN_TEMPERATURE;
                switch (grmt_hta_data.data[HITACHI_IRP_ACTICE_BYTE])
                {
                    case HITACHI_IRP_ACTIVE_VALUE:
                        active = 1;
                        memcpy(grmt_htaBuffer, grmt_hta_data.data,
                               sizeof(grmt_htaBuffer));
                        break;
                    case HITACHI_IRP_INACTIVE_VALUE:
                        active = 0;
                        memcpy(grmt_htaBuffer, grmt_hta_data.data,
                               sizeof(grmt_htaBuffer));
                        break;
                    default:
                        break;
                }
                switch (grmt_hta_data.data[HITACHI_IRP_STATE_BYTE] & 0x0F)
                {
                    case HITACHI_IRP_COOLER_VALUE:
                        mode = HITACHI_AC_MODE_COOLER;
                        break;
                    case HITACHI_IRP_HEATER_VALUE:
                        mode = HITACHI_AC_MODE_HEATER;
                        break;
                    default:
                        mode = HITACHI_AC_MODE_AUTO;
                        break;
                }
                switch (grmt_hta_data.data[HITACHI_IRP_STATE_BYTE] & 0xF0)
                {
                    case HITACHI_IRP_FANHI_VALUE:
                        speed = HITACHI_AC_MAX_FAN_SPEED;
                        break;
                    case HITACHI_IRP_FANMD_VALUE:
                        speed = HITACHI_AC_MID_FAN_SPEED;
                        break;
                    case HITACHI_IRP_FANLO_VALUE:
                        speed = HITACHI_AC_MIN_FAN_SPEED;
                        break;
                    case HITACHI_IRP_FANEC_VALUE:
                        speed = HITACHI_AC_SLI_FAN_SPEED;
                        break;
                    default:
                        speed = HITACHI_AC_AUTO_FAN_SPEED;
                        break;
                }
                if (grmt_hta_data.data[HITACHI_IRP_OPCODE_BYTE] ==
                    HITACHI_IRP_OPCODE_SWING_VALUE)
                {
                    switch (grmt_hta_data.data[HITACHI_IRP_SWING_UD_BYTE] &
                            0x20)
                    {
                        case HITACHI_IRP_SWING_VALUE:
                            swing = true;
                            break;
                        case HITACHI_IRP_NOSWING_VALUE:
                            swing = false;
                            break;
                        default:
                            swing = -1;
                            break;
                    }
                }

                if (active == 0)
                {
                    /* Received turn off from remote controller
                       Switch to fan only 10 min */
                    rmt_hattg_msg_t tghitmsg;
                    memset(&tghitmsg, 0, sizeof(rmt_hattg_msg_t));
                    tghitmsg.bactivech = true;
                    tghitmsg.active = active;
                    syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_INFO,
                                   "IR turn off Hitachi AC");
                    ir_hitachiac_tigger(tghitmsg);
                }
                else
                {
                    if (ghitachiac_delay_timer_handle != NULL)
                    {
                        /* Received turn on from remote controller then stop
                         * timer for fan only 5 min */
                        if (esp_timer_is_active(ghitachiac_delay_timer_handle))
                        {
                            syslog_handler(SYSLOG_FACILITY_RMT,
                                           SYSLOG_LEVEL_DEBUG,
                                           "IR stop fan only timer");
                            ESP_ERROR_CHECK(
                                esp_timer_stop(ghitachiac_delay_timer_handle));
                        }
                    }
                    syslog_handler(
                        SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_INFO,
                        "IR turn on Hitachi AC Mode: %s, Temp: %d, Speed: %d",
                        mode == HITACHI_AC_MODE_HEATER   ? "Haeter"
                        : mode == HITACHI_AC_MODE_COOLER ? "Cooler"
                                                         : "Others",
                        temp, speed);
                }
                /* Save Global variable */
                rmt_setiracstatus(active);
                rmt_setiracmode(mode);
                rmt_setiracspeed(speed);
                rmt_setiracswing(swing);
                rmt_setiractemp(temp);

                /* Save to NVDB */
                ac_saveconfig(AC_NVS_STATUS_KEY, active);
                ac_saveconfig(AC_NVS_TYPE_KEY, mode);
                ac_saveconfig(AC_NVS_TEMP_KEY, temp);
                ac_saveconfig(AC_NVS_SPEED_KEY, speed);
                ac_saveconfig(AC_NVS_SWING_KEY, swing);

                /* Update to HAP */
                ir_update_hap_Hitachi_status(active, temp, mode, speed, swing);
            }
            break;
        case IR_TYPE_ZERO:
            if (!IS_BATHROOM(sys_mac))
            {
                if ((grmt_zro_data.data[0] == 0x76) &&
                    (grmt_zro_data.data[1] == 0x80))
                {
                    active = rmt_iszerofanactive();
                    active = (active == 1) ? 0 : 1;
                    rmt_setzerofanstatus((bool)active);
                    if (ld2410_isOccupancyStatus())
                    {
                        hap_setelfoccupancyfanstatus((bool)active);
                        elf_saveconfig(ELF_NVS_OCCUPANCYFANSTATUS_KEY,
                                       hap_iselfoccupancyfanactive());
                    }
                    syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_INFO,
                                   "IR change +-0 Fan status");
                    zerofan_saveconfig(ZEROFAN_NVS_STATUS_KEY, active);
                }
                if ((grmt_zro_data.data[0] == 0x76) &&
                    (grmt_zro_data.data[1] == 0x40))
                {
                    rmt_getzerofanspeed(&speed);
                    speed = (speed + 1) > 9 ? 9 : (speed + 1);
                    rmt_setzerofanspeed(speed);
                    syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_INFO,
                                   "IR increase +-0 Fan speed");
                    zerofan_saveconfig(ZEROFAN_NVS_SPEED_KEY, speed);
                }
                if ((grmt_zro_data.data[0] == 0x76) &&
                    (grmt_zro_data.data[1] == 0x62))
                {
                    rmt_getzerofanspeed(&speed);
                    speed = (speed - 1 > 0) ? (speed - 1) : 1;
                    rmt_setzerofanspeed(speed);
                    syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_INFO,
                                   "IR decrease +-0 Fan speed");
                    zerofan_saveconfig(ZEROFAN_NVS_SPEED_KEY, speed);
                }
                if ((grmt_zro_data.data[0] == 0x76) &&
                    (grmt_zro_data.data[1] == 0xc2))
                {
                    rmt_getzerofanswing(&swing);
                    swing = (swing) ? 0 : 1;
                    rmt_setzerofanswing(swing);
                    syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_INFO,
                                   "IR change +-0 Fan swing");
                    zerofan_saveconfig(ZEROFAN_NVS_SWING_KEY, swing);
                }
                ir_update_hap_zerofan_status(active, speed, swing);
            }
            break;
        case IR_TYPE_DELTA:
            uint32_t deltaactivetimer = 0;
            char syslogstr[128] = {};
            if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
            {
                switch (grmt_delta_data.data[2])
                {
                    case 0x12:
                        snprintf(syslogstr + strlen(syslogstr),
                                 sizeof(syslogstr) - strlen(syslogstr),
                                 "IR turn off Delta fan");
                        ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_MANUAL,
                                           IR_DELTA_FAN_TIGGER_ACTIVE_OFF,
                                           IR_DELTA_FAN_DURATION_FOREVER);
                        rmt_setmanualfanstatus(false);
                        break;
                    case 0x19:
                        snprintf(syslogstr + strlen(syslogstr),
                                 sizeof(syslogstr) - strlen(syslogstr),
                                 "IR turn on Delta fan exhaust");
                        ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_MANUAL,
                                           IR_DELTA_FAN_TIGGER_ACTIVE_ON,
                                           IR_DELTA_FAN_DURATION_FOREVER);
                        rmt_setmanualfanstatus(true);
                        break;
                    case 0x13:
                        snprintf(syslogstr + strlen(syslogstr),
                                 sizeof(syslogstr) - strlen(syslogstr),
                                 "IR turn on Delta fan warm");
                        ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_MANUAL,
                                           IR_DELTA_FAN_TIGGER_ACTIVE_ON,
                                           IR_DELTA_FAN_DURATION_FOREVER);
                        rmt_setmanualfanstatus(true);
                        break;
                    case 0x15:
                        snprintf(syslogstr + strlen(syslogstr),
                                 sizeof(syslogstr) - strlen(syslogstr),
                                 "IR turn on Delta fan dry");
                        ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_MANUAL,
                                           IR_DELTA_FAN_TIGGER_ACTIVE_ON,
                                           IR_DELTA_FAN_DURATION_FOREVER);
                        rmt_setmanualfanstatus(true);
                        break;
                    default:
                        snprintf(syslogstr + strlen(syslogstr),
                                 sizeof(syslogstr) - strlen(syslogstr),
                                 "IR turn on Delta fan other");
                        rmt_setmanualfanstatus(true);
                        break;
                }
                deltafan_saveconfig(DELTAFAN_NVS_STATUS_KEY,
                                    rmt_ismanualfanactive());
                if (rmt_ismanualfanactive())
                {
                    switch (grmt_delta_data.time[2])
                    {
                        case 0x01: /* 0.5HR */
                            snprintf(syslogstr + strlen(syslogstr),
                                     sizeof(syslogstr) - strlen(syslogstr),
                                     " 30 minutes manually");
                            deltaactivetimer = (30 * 60); /* Sec */
                            break;
                        case 0x02: /* 1HR */
                            snprintf(syslogstr + strlen(syslogstr),
                                     sizeof(syslogstr) - strlen(syslogstr),
                                     " 1 hours manually");
                            deltaactivetimer = (60 * 60); /* Sec */
                            break;
                        case 0x03: /* 2HR */
                            snprintf(syslogstr + strlen(syslogstr),
                                     sizeof(syslogstr) - strlen(syslogstr),
                                     " 2 hours manually");
                            deltaactivetimer = (2 * 60 * 60); /* Sec */
                            break;
                        case 0x04: /* 3HR */
                            snprintf(syslogstr + strlen(syslogstr),
                                     sizeof(syslogstr) - strlen(syslogstr),
                                     " 3 hours manually");
                            deltaactivetimer = (3 * 60 * 60); /* Sec */
                            break;
                        case 0x05: /* 4HR */
                            snprintf(syslogstr + strlen(syslogstr),
                                     sizeof(syslogstr) - strlen(syslogstr),
                                     " 4 hours manually");
                            deltaactivetimer = (4 * 60 * 60); /* Sec */
                            break;
                        case 0x06: /* 6HR */
                            snprintf(syslogstr + strlen(syslogstr),
                                     sizeof(syslogstr) - strlen(syslogstr),
                                     " 6 hours manually");
                            deltaactivetimer = (6 * 60 * 60); /* Sec */
                            break;
                        case 0x07: /* Forever */
                            snprintf(syslogstr + strlen(syslogstr),
                                     sizeof(syslogstr) - strlen(syslogstr),
                                     " continued manually");
                            deltaactivetimer = 0;
                            break;
                        default:
                            snprintf(syslogstr + strlen(syslogstr),
                                     sizeof(syslogstr) - strlen(syslogstr),
                                     " unknown time manually");
                            deltaactivetimer = 0;
                            break;
                    }
                }

                if (gsync_delta_manual_timer_handle != NULL)
                {
                    if (esp_timer_is_active(gsync_delta_manual_timer_handle))
                    {
                        ESP_ERROR_CHECK(
                            esp_timer_stop(gsync_delta_manual_timer_handle));
                    }
                }
                else
                {
                    ESP_ERROR_CHECK(
                        esp_timer_create(&sync_delta_manual_timer_args,
                                         &gsync_delta_manual_timer_handle));
                }

                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_INFO,
                               syslogstr);

                if (deltaactivetimer && rmt_ismanualfanactive())
                {
                    // Set working time is not forever, start timer
                    ESP_ERROR_CHECK(
                        esp_timer_start_once(gsync_delta_manual_timer_handle,
                                             (deltaactivetimer * 1000 * 1000)));
                    syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                                   "Start delta manual fan %d timer",
                                   deltaactivetimer);
                }

                /* TODO 1CE3 is delta_swing */
                // zerofan_saveconfig(ZEROFAN_NVS_STATUS_KEY,gzerofanstatus);
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Check whether a RMT symbol represents NEC logic zero
 */
static bool ir_parse_logic0(rmt_symbol_word_t *rmt_nec_symbols, char type)
{
    int zero_dur_0 = 0;
    int zero_dur_1 = 0;
    switch (type)
    {
        case IR_TYPE_HITACHI:
            zero_dur_0 = HTA_PAYLOAD_ZERO_DURATION_0;
            zero_dur_1 = HTA_PAYLOAD_ZERO_DURATION_1;
            break;
        case IR_TYPE_ZERO:
            zero_dur_0 = ZERO_PAYLOAD_ZERO_DURATION_0;
            zero_dur_1 = ZERO_PAYLOAD_ZERO_DURATION_1;
            break;
        case IR_TYPE_DELTA:
            zero_dur_0 = DELTA_PAYLOAD_ZERO_DURATION_0;
            zero_dur_1 = DELTA_PAYLOAD_ZERO_DURATION_1;
            break;
        case IR_TYPE_DYSON:
            zero_dur_0 = DYSON_PAYLOAD_ZERO_DURATION_0;
            zero_dur_1 = DYSON_PAYLOAD_ZERO_DURATION_1;
            break;
        default:
            break;
    }
    return ir_check_in_range(rmt_nec_symbols->duration0, zero_dur_0) &&
           ir_check_in_range(rmt_nec_symbols->duration1, zero_dur_1);
}

/**
 * @brief Check whether a RMT symbol represents NEC logic one
 */
static bool ir_parse_logic1(rmt_symbol_word_t *rmt_nec_symbols, char type)
{
    int one_dur_0 = 0;
    int one_dur_1 = 0;
    switch (type)
    {
        case IR_TYPE_HITACHI:
            one_dur_0 = HTA_PAYLOAD_ONE_DURATION_0;
            one_dur_1 = HTA_PAYLOAD_ONE_DURATION_1;
            break;
        case IR_TYPE_ZERO:
            one_dur_0 = ZERO_PAYLOAD_ONE_DURATION_0;
            one_dur_1 = ZERO_PAYLOAD_ONE_DURATION_1;
            break;
        case IR_TYPE_DELTA:
            one_dur_0 = DELTA_PAYLOAD_ONE_DURATION_0;
            one_dur_1 = DELTA_PAYLOAD_ONE_DURATION_1;
            break;
        case IR_TYPE_DYSON:
            one_dur_0 = DYSON_PAYLOAD_ONE_DURATION_0;
            one_dur_1 = DYSON_PAYLOAD_ONE_DURATION_1;
            break;
        default:
            break;
    }
    return ir_check_in_range(rmt_nec_symbols->duration0, one_dur_0) &&
           ir_check_in_range(rmt_nec_symbols->duration1, one_dur_1);
}

static bool ir_rmt_rx_done_callback(rmt_channel_handle_t channel,
                                    const rmt_rx_done_event_data_t *edata,
                                    void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t rmt_rx_queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(rmt_rx_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static bool ir_rmt_tx_done_callback(rmt_channel_handle_t channel,
                                    const rmt_tx_done_event_data_t *edata,
                                    void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t tranmit_queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(tranmit_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

/* Use IR configuration to update Homekit */
static void ir_update_hap_Hitachi_status(int active, int temp, int mode,
                                         int speed, int swing)
{
    hap_update_value(HAP_ACCESSORY_HITACHI_AC, HAP_CHARACTER_ACTIVE, &active);
    if (active)
    {
        hap_update_value(HAP_ACCESSORY_HITACHI_AC,
                         HAP_CHARACTER_HEATING_THRESHOLD, &temp);
        hap_update_value(HAP_ACCESSORY_HITACHI_AC,
                         HAP_CHARACTER_COOLING_THRESHOLD, &temp);
        hap_update_value(HAP_ACCESSORY_HITACHI_AC, HAP_CHARACTER_MODE, &mode);
        hap_update_value(HAP_ACCESSORY_HITACHI_AC, HAP_CHARACTER_FAN_SPEED,
                         &speed);
        if (swing >= 0)
        {
            hap_update_value(HAP_ACCESSORY_HITACHI_AC, HAP_CHARACTER_FAN_SWING,
                             &swing);
        }
    }
}

static void ir_update_hap_zerofan_status(int active, int speed, int swing)
{
    hap_update_value(HAP_ACCESSORY_ZERO_FAN, HAP_CHARACTER_ACTIVE, &active);
    hap_update_value(HAP_ACCESSORY_ZERO_FAN, HAP_CHARACTER_FAN_SWING, &swing);
    hap_update_value(HAP_ACCESSORY_ZERO_FAN, HAP_CHARACTER_FAN_SPEED, &speed);
}

static void ir_update_deltafan_status(int active, int speed)
{
    uint8_t sys_mac[6];

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
    {
        hap_update_value(HAP_ACCESSORY_DELTA_FAN, HAP_CHARACTER_IGNORE,
                         &active);
    }
}

void task_rmt(void *pvParameters)
{
    // RMT init
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = (IR_RESOLUTION_HZ),
        .mem_block_symbols =
            RMT_RX_MEM_BLK_SYMB,  // amount of RMT symbols that the channel can
                                  // store at a time
        .flags.with_dma = false,
        .gpio_num = RMT_RX_GPIO_NUM,
    };
    rmt_channel_handle_t rx_channel = NULL;
    int beepwr[RMT_CHECK_TIMES] = {0}, retry = 0, checkbee = 0;

    gsemaRMTCfg = xSemaphoreCreateBinary();
    if (gsemaRMTCfg != NULL)
    {
        xSemaphoreGive(gsemaRMTCfg);
    }

    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &rx_channel));

    ESP_LOGI(TAG_IR, "register RX done callback");

    gqueue_rmt_tx = xQueueCreate(10, sizeof(rmt_msg_t));

    QueueHandle_t rmt_rx_queue =
        xQueueCreate(5, sizeof(rmt_rx_done_event_data_t));
    assert(gqueue_rmt_tx);
    assert(rmt_rx_queue);
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = ir_rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(
        rmt_rx_register_event_callbacks(rx_channel, &cbs, rmt_rx_queue));

    // the following timing requirement is based on NEC protocol
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns =
            1250,  // the shortest duration for NEC signal is 560us, 1250ns <
                   // 560us, valid signal won't be treated as noise
        .signal_range_max_ns =
            50000000, /* the longest duration for NEC signal is 9000us,
                         12000000ns > 9000us, the receive won't stop early
                         Adjust the max ns from 12,000,000 to 50,000,000 for
                         delta fan IR. 2025/03/11 */
    };

    ESP_LOGI(TAG_IR, "create RMT TX channel");
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols =
            RMT_TX_MEM_BLK_SYMB,  // amount of RMT symbols that the channel can
                                  // store at a time
        .trans_queue_depth =
            4,  // number of transactions that allowed to pending in the
                // background, this example won't queue multiple transactions,
                // so queue depth > 1 is sufficient
        .flags.with_dma = false,
        .gpio_num = RMT_TX_GPIO_NUM,
    };
    rmt_channel_handle_t tx_channel = NULL;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &tx_channel));

    ESP_LOGI(TAG_IR, "register TX done callback");
    QueueHandle_t transmit_queue =
        xQueueCreate(5, sizeof(rmt_tx_done_event_data_t));
    assert(transmit_queue);
    rmt_tx_event_callbacks_t tcbs = {
        .on_trans_done = ir_rmt_tx_done_callback,
    };
    ESP_ERROR_CHECK(
        rmt_tx_register_event_callbacks(tx_channel, &tcbs, transmit_queue));

    ESP_LOGI(TAG_IR, "modulate carrier to TX channel");
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000,  // 38KHz
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_channel, &carrier_cfg));

    // Won't send frames in a loop
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,  // no loop
    };

    ESP_LOGI(TAG_IR, "install IR NEC encoder");
    ir_hta_encoder_config_t hta_encoder_cfg = {
        .resolution = IR_RESOLUTION_HZ,
    };
    ir_zro_encoder_config_t zro_encoder_cfg = {
        .resolution = IR_RESOLUTION_HZ,
    };
    ir_delta_encoder_config_t delta_encoder_cfg = {
        .resolution = IR_RESOLUTION_HZ,
    };
    rmt_encoder_handle_t hta_encoder = NULL;
    rmt_encoder_handle_t zro_encoder = NULL;
    rmt_encoder_handle_t delta_encoder = NULL;

    ESP_ERROR_CHECK(rmt_new_ir_hta_encoder(&hta_encoder_cfg, &hta_encoder));
    ESP_ERROR_CHECK(rmt_new_ir_zro_encoder(&zro_encoder_cfg, &zro_encoder));
    ESP_ERROR_CHECK(
        rmt_new_ir_delta_encoder(&delta_encoder_cfg, &delta_encoder));

    ESP_LOGI(TAG_IR, "enable RMT TX and RX channels");
    ESP_ERROR_CHECK(rmt_enable(tx_channel));
    ESP_ERROR_CHECK(rmt_enable(rx_channel));
    rmt_rx_gpio_enable();

    // save the received RMT symbols
    static rmt_symbol_word_t
        raw_symbols[RMT_RX_MEM_BLK_SYMB];  // 64 symbols should be sufficient
                                           // for a standard NEC frame
    rmt_rx_done_event_data_t rx_data;
    rmt_tx_done_event_data_t tx_data;
    // ready to receive
    rmt_restart_receive(rx_channel, raw_symbols, sizeof(raw_symbols),
                        &receive_config);
    system_task_created(TASK_RMT_ID);
    system_task_all_ready();

    while (1)
    {
        rmt_msg_t rmt_msg = {};

        // wait for RX done signal
        if (rmt_rx_queue && xQueueReceive(rmt_rx_queue, &rx_data,
                                          pdMS_TO_TICKS(1000)) == pdPASS)
        {
            // parse the receive symbols and print the result
            ir_parse_ir_frame(rx_data.received_symbols, rx_data.num_symbols);
            rmt_restart_receive(rx_channel, raw_symbols, sizeof(raw_symbols),
                                &receive_config);
        }

        if (gqueue_rmt_tx &&
            xQueueReceive(gqueue_rmt_tx, &rmt_msg, pdMS_TO_TICKS(5)) == pdPASS)
        {
            retry = 0;
            rmt_form_tx_data(&rmt_msg);
            /* Pendding LD2410 */
            if (xSemaphoreTake(gsemaLD2410, portMAX_DELAY) == pdTRUE)
            {
                rmt_rx_gpio_disable();
                ESP_ERROR_CHECK(rmt_disable(rx_channel));
                do
                {
                    checkbee = 0;
                    memset(beepwr, 0, sizeof(beepwr));
                    if (rmt_msg.type == IR_TYPE_HITACHI)
                    {
                        ESP_ERROR_CHECK(rmt_transmit(
                            tx_channel, hta_encoder, &rmt_msg,
                            sizeof(rmt_msg.data), &transmit_config));
                    }
                    if (rmt_msg.type == IR_TYPE_ZERO)
                    {
                        ESP_ERROR_CHECK(rmt_transmit(tx_channel, zro_encoder,
                                                     &rmt_msg, 2,
                                                     &transmit_config));
                    }
                    if (rmt_msg.type == IR_TYPE_DELTA)
                    {
                        ESP_ERROR_CHECK(rmt_transmit(tx_channel, delta_encoder,
                                                     &rmt_msg, sizeof(rmt_msg),
                                                     &transmit_config));
                    }

                    do
                    {
                        max9814_check_bee(rmt_msg.targetfreq, 0,
                                          &beepwr[checkbee], NULL);
                        checkbee++;
                    } while (RMT_ISNOT_HEAR && checkbee < RMT_CHECK_TIMES);

                    if (xQueueReceive(transmit_queue, &tx_data,
                                      pdMS_TO_TICKS(100)) != pdPASS)
                    {
                        syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_ERROR,
                                       "TX fail");
                    }
                    syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                                   "TX %d.%d bee %d Hz >%d pwr = %d,%d,%d",
                                   retry + 1, checkbee, rmt_msg.targetfreq,
                                   rmt_msg.pwrthreshold, beepwr[0], beepwr[1],
                                   beepwr[2]);

                    rmt_tx_wait_all_done(tx_channel, 50);

                    retry++;
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                } while (RMT_ISNOT_HEAR && retry < RMT_RETRY_TIMES);
                ESP_ERROR_CHECK(rmt_enable(rx_channel));
                rmt_rx_gpio_enable();
                xSemaphoreGive(gsemaLD2410);
            }
            rmt_restart_receive(rx_channel, raw_symbols, sizeof(raw_symbols),
                                &receive_config);
        }
    }
    vTaskDelete(NULL);
}

int ir_hitachiac_tigger(rmt_hattg_msg_t msg)
{
    rmt_msg_t rmt_msg;
    int ori_mode = 0, ori_actype = 0, ori_acspeed = 0;
    esp_timer_create_args_t hitachiac_delay_timer_args = {
        .callback = &ir_hitachiac_delay_timer_callback,
        .name = "hitachiac_delay_timer"};
#if 0    
    dbg_printf("\n Hitachi Tigger Message:\n");
    dbg_printf("   Active:%d, %d\n",msg.bactivech, msg.active);
    dbg_printf("     Mode:%d, %d\n",msg.bmodech, msg.mode);
    dbg_printf("  PreMode:%d\n",grmthitachiac_premode);
    dbg_printf("  LowTemp:%d, %d\n",msg.blowtempch, msg.lowtemp);
    dbg_printf(" HighTemp:%d, %d\n",msg.bhightempch, msg.hightemp);
    dbg_printf("    Speed:%d, %d\n",msg.bfanspeedch, msg.fanspeed);
    dbg_printf("    Swing:%d, %d\n\n",msg.bswingch, msg.swing);
#endif
    memset(&rmt_msg, 0, sizeof(rmt_msg_t));
    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG, "Tigger Hitachi IR");

    if (msg.bactivech)
    {
        rmt_msg.bstatusch = true;
        if (ghitachiac_delay_timer_handle != NULL)
        {
            if (esp_timer_is_active(ghitachiac_delay_timer_handle))
            {
                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                               "Hitachi AC tiggered stop fan only timer");
                ESP_ERROR_CHECK(esp_timer_stop(ghitachiac_delay_timer_handle));
            }
        }
        else
        {
            ESP_ERROR_CHECK(esp_timer_create(&hitachiac_delay_timer_args,
                                             &ghitachiac_delay_timer_handle));
        }

        if (msg.active == HAP_ACTIVE_ON)
        {
            /* Turn on AC */
            rmt_msg.status = true;

            /* If tigger command doesn't identify mode and fan speed,
               get from global variable */
            if (msg.bmodech != true)
            {
                rmt_msg.bmodech = true;
                rmt_getiracmode(&ori_actype);
                rmt_msg.mode = ori_actype;
            }
            if (msg.bfanspeedch != true)
            {
                rmt_msg.bfanspeedch = true;
                rmt_getiracspeed(&ori_acspeed);
                rmt_msg.fanspeed = ori_acspeed;
            }
        }
        else if (msg.active == HAP_ACTIVE_PRE_OFF)
        {
            rmt_getiracmode(&ori_mode);
            if ((ori_mode == HITACHI_AC_MODE_COOLER) ||
                (ori_mode == HITACHI_AC_MODE_AUTO))
            {
                rmt_msg.status = true;
                rmt_msg.bmodech = true;
                rmt_msg.mode = HITACHI_AC_MODE_FAN_ONLY;
                rmt_msg.bfanspeedch = true;
                rmt_msg.fanspeed = HITACHI_AC_MAX_FAN_SPEED;
                /* Start 10 minutes timer (5*60*1000*1000us）*/
                syslog_handler(
                    SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                    "Hitachi AC tiggered off from %s, set fan only 10 minutes",
                    (ori_mode == HAP_AC_MODE_COOLER ? "Cooler" : "Auto"));
                ESP_ERROR_CHECK(esp_timer_start_once(
                    ghitachiac_delay_timer_handle, (10 * 60 * 1000 * 1000)));
            }
            else
            {
                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                               "Hitachi AC tiggered off from Heater/Off mode");
                rmt_msg.status = false;
            }
        }
        else if (msg.active == HAP_ACTIVE_OFF)
        {
            syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                           "Hitachi AC tiggered off from pre off mode");
            rmt_msg.status = false;
        }
    }

    if (msg.bmodech)
    {
        syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                       "Hitachi AC tiggered mode %d", msg.mode);
        rmt_msg.bmodech = true;
        switch (msg.mode)
        {
            case HAP_AC_MODE_AUTO:
                rmt_msg.mode = HITACHI_AC_MODE_AUTO;
                break;
            case HAP_AC_MODE_HEATER:
                rmt_msg.mode = HITACHI_AC_MODE_HEATER;
                break;
            case HAP_AC_MODE_COOLER:
                rmt_msg.mode = HITACHI_AC_MODE_COOLER;
                break;
            default:
                rmt_msg.mode = HITACHI_AC_MODE_AUTO;
                break;
        }
    }

    if (msg.blowtempch)
    {
        rmt_msg.blothch = true;
        rmt_msg.loth = msg.lowtemp;
    }

    if (msg.bhightempch)
    {
        rmt_msg.bhithch = true;
        rmt_msg.hith = msg.hightemp;
    }

    if (msg.bfanspeedch)
    {
        syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                       "Hitachi AC tiggered speed %d", msg.fanspeed);
        rmt_msg.bfanspeedch = true;
        rmt_msg.fanspeed = msg.fanspeed;
    }

    if (msg.bswingch)
    {
        rmt_msg.bswingch = true;
        rmt_msg.swing = msg.swing;
    }
    rmt_msg.type = IR_TYPE_HITACHI;
    rmt_msg.targetfreq = MAX9814_HITACHI_AC_BEE_FREQ;
    rmt_msg.pwrthreshold = MAX9814_HITACHI_AC_BEE_THRESHOLD;
    /* SendIR */
    if (!rmt_enqueue_msg(&rmt_msg))
    {
        syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_ERROR,
                       "EnQueue Hitachi IR fail");
        return SYSTEM_ERROR_NOT_READY;
    }
    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                   "EnQueue Hitachi IR %d", rmt_msg.type);
    return SYSTEM_ERROR_NONE;
}

int rmt_form_tx_data(rmt_msg_t *rmt_msg)
{
    uint16_t HtaTempGap = HITACHI_GAP_TEMPERATURE;
    int target_temp = 0;
    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                   "Form %d data (1.Hitachi, 2.+-0, 3.Delta)", rmt_msg->type);
    switch (rmt_msg->type)
    {
        case IR_TYPE_HITACHI:
            if (rmt_msg->bstatusch)
            {
                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                               "Form IR AC status %d", rmt_msg->status);
                if (rmt_msg->status)
                {
                    /* Turn on AC */
                    grmt_htaBuffer[HITACHI_IRP_ACTICE_BYTE] =
                        HITACHI_IRP_ACTIVE_VALUE;
                    grmt_htaBuffer[HITACHI_IRP_ACTICE_BYTE + 1] =
                        ~grmt_htaBuffer[HITACHI_IRP_ACTICE_BYTE];
                }
                else if (!rmt_msg->status)
                {
                    grmt_htaBuffer[HITACHI_IRP_ACTICE_BYTE] =
                        HITACHI_IRP_INACTIVE_VALUE;
                    grmt_htaBuffer[HITACHI_IRP_ACTICE_BYTE + 1] =
                        ~grmt_htaBuffer[HITACHI_IRP_ACTICE_BYTE];
                }
            }

            if (rmt_msg->bmodech)
            {
                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                               "Form IR AC Mode %d", rmt_msg->mode);
                switch (rmt_msg->mode)
                {
                    case HITACHI_AC_MODE_HEATER:
                        rmt_getiractemp(&target_temp);
                        grmt_htaBuffer[HITACHI_IRP_TEMPERATURE_BYTE_HI] =
                            ((target_temp - HITACHI_DEF_TEMPERATURE) *
                                 HtaTempGap +
                             HITACHI_DEF_TEMPERATURE_HILO) >>
                            8;
                        grmt_htaBuffer[HITACHI_IRP_TEMPERATURE_BYTE_LO] =
                            (target_temp - HITACHI_DEF_TEMPERATURE) *
                                HtaTempGap +
                            HITACHI_DEF_TEMPERATURE_HILO;
                        break;
                    case HITACHI_AC_MODE_COOLER:
                        rmt_getiractemp(&target_temp);
                        grmt_htaBuffer[HITACHI_IRP_TEMPERATURE_BYTE_HI] =
                            ((target_temp - HITACHI_DEF_TEMPERATURE) *
                                 HtaTempGap +
                             HITACHI_DEF_TEMPERATURE_HILO) >>
                            8;
                        grmt_htaBuffer[HITACHI_IRP_TEMPERATURE_BYTE_LO] =
                            (target_temp - HITACHI_DEF_TEMPERATURE) *
                                HtaTempGap +
                            HITACHI_DEF_TEMPERATURE_HILO;
                        break;
                    default:
                        break;
                }
                grmt_htaBuffer[HITACHI_IRP_STATE_BYTE] =
                    grmt_htaBuffer[HITACHI_IRP_STATE_BYTE] &
                    0xF0; /* Clear data */
                grmt_htaBuffer[HITACHI_IRP_STATE_BYTE] =
                    grmt_htaBuffer[HITACHI_IRP_STATE_BYTE] | rmt_msg->mode;
                grmt_htaBuffer[HITACHI_IRP_STATE_BYTE + 1] =
                    ~grmt_htaBuffer[HITACHI_IRP_STATE_BYTE];
            }

            if (rmt_msg->blothch)
            {
                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                               "Form IR AC Low Temperature threshold %d",
                               rmt_msg->loth);
                grmt_htaBuffer[HITACHI_IRP_TEMPERATURE_BYTE_HI] =
                    ((rmt_msg->loth - HITACHI_DEF_TEMPERATURE) * HtaTempGap +
                     HITACHI_DEF_TEMPERATURE_HILO) >>
                    8;
                grmt_htaBuffer[HITACHI_IRP_TEMPERATURE_BYTE_LO] =
                    (rmt_msg->loth - HITACHI_DEF_TEMPERATURE) * HtaTempGap +
                    HITACHI_DEF_TEMPERATURE_HILO;
            }

            if (rmt_msg->bhithch)
            {
                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                               "Form IR AC High Temperature threshold %d",
                               rmt_msg->hith);
                grmt_htaBuffer[HITACHI_IRP_TEMPERATURE_BYTE_HI] =
                    ((rmt_msg->hith - HITACHI_DEF_TEMPERATURE) * HtaTempGap +
                     HITACHI_DEF_TEMPERATURE_HILO) >>
                    8;
                grmt_htaBuffer[HITACHI_IRP_TEMPERATURE_BYTE_LO] =
                    (rmt_msg->hith - HITACHI_DEF_TEMPERATURE) * HtaTempGap +
                    HITACHI_DEF_TEMPERATURE_HILO;
            }

            if (rmt_msg->bfanspeedch)
            {
                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                               "Form IR AC Fan Speed %d", rmt_msg->fanspeed);
                grmt_htaBuffer[HITACHI_IRP_STATE_BYTE] =
                    grmt_htaBuffer[HITACHI_IRP_STATE_BYTE] &
                    0x0F; /* Clear data */
                grmt_htaBuffer[HITACHI_IRP_STATE_BYTE] =
                    grmt_htaBuffer[HITACHI_IRP_STATE_BYTE] |
                    ((rmt_msg->fanspeed) << 4);
                grmt_htaBuffer[HITACHI_IRP_STATE_BYTE + 1] =
                    ~grmt_htaBuffer[HITACHI_IRP_STATE_BYTE];
            }

            if (rmt_msg->bswingch)
            {
                syslog_handler(SYSLOG_FACILITY_RMT, SYSLOG_LEVEL_DEBUG,
                               "Form IR AC Fan Swing %d", rmt_msg->swing);
                grmt_htaBuffer[HITACHI_IRP_OPCODE_BYTE] =
                    HITACHI_IRP_OPCODE_SWING_VALUE;
                grmt_htaBuffer[HITACHI_IRP_OPCODE_BYTE + 1] =
                    ~grmt_htaBuffer[HITACHI_IRP_OPCODE_BYTE];
                grmt_htaBuffer[HITACHI_IRP_SWING_UD_BYTE] =
                    grmt_htaBuffer[HITACHI_IRP_SWING_UD_BYTE] &
                    0xDF; /* Clear data */
                grmt_htaBuffer[HITACHI_IRP_SWING_UD_BYTE] =
                    grmt_htaBuffer[HITACHI_IRP_SWING_UD_BYTE] |
                    ((rmt_msg->swing) << HITACHI_IRP_SWING_UD_BIT);
                grmt_htaBuffer[HITACHI_IRP_SWING_UD_BYTE + 1] =
                    ~grmt_htaBuffer[HITACHI_IRP_SWING_UD_BYTE];
                grmt_htaBuffer[HITACHI_IRP_OPCODE_BYTE] =
                    HITACHI_IRP_OPCODE_ACTIVE_VALUE;
                grmt_htaBuffer[HITACHI_IRP_OPCODE_BYTE + 1] =
                    ~grmt_htaBuffer[HITACHI_IRP_OPCODE_BYTE];
            }
            memcpy(rmt_msg->data, grmt_htaBuffer, 44);
            break;
        case IR_TYPE_DELTA:
            if (rmt_msg->bmodech)
            {
                switch (rmt_msg->mode)
                {
                    case IR_DELTA_FAN_TIGGER_MODE_OFF:
                        grmt_deltaBuffer[2] = DELTA_FAN_MODE_OFF;
                        grmt_deltaBuffer[3] = ~DELTA_FAN_MODE_OFF;
                        rmt_setmanualfanstatus(false);
                        break;
                    case IR_DELTA_FAN_TIGGER_MODE_EXHAUST:
                    case IR_DELTA_FAN_TIGGER_MODE_HOMEKIT:
                        /* Exhaust 1HR */
                        grmt_deltaBuffer[2] = DELTA_FAN_MODE_EXHAUST_MAX;
                        grmt_deltaBuffer[3] = ~DELTA_FAN_MODE_EXHAUST_MAX;
                        rmt_setmanualfanstatus(true);
                        break;
                    case IR_DELTA_FAN_TIGGER_MODE_WARM:
                        /* Warm 3HR */
                        grmt_deltaBuffer[2] = DELTA_FAN_MODE_HEATER_MAX;
                        grmt_deltaBuffer[3] = ~DELTA_FAN_MODE_HEATER_MAX;
                        rmt_setmanualfanstatus(true);
                        break;
                    case IR_DELTA_FAN_TIGGER_MODE_DRY:
                        /* Dry 3HR */
                        /* Using exhaust for dry
                        grmt_deltaBuffer[2]=DELTA_FAN_MODE_DRY_MAX;
                        grmt_deltaBuffer[3]=~DELTA_FAN_MODE_DRY_MAX;
                        Using exhaust for dry */
                        grmt_deltaBuffer[2] = DELTA_FAN_MODE_EXHAUST_MAX;
                        grmt_deltaBuffer[3] = ~DELTA_FAN_MODE_EXHAUST_MAX;
                        rmt_setmanualfanstatus(true);
                        break;
                    case IR_DELTA_FAN_TIGGER_MODE_MANUAL:
                    default:
                        break;
                }

                switch (rmt_msg->duration)
                {
                    case IR_DELTA_FAN_DURATION_HALF_HOUR:
                        grmt_deltatimerBuffer[2] = DELTA_FAN_DURATION_HALF_HOUR;
                        grmt_deltatimerBuffer[3] =
                            ~DELTA_FAN_DURATION_HALF_HOUR;
                        break;
                    case IR_DELTA_FAN_DURATION_1HR:
                        grmt_deltatimerBuffer[2] = DELTA_FAN_DURATION_ONE_HOUR;
                        grmt_deltatimerBuffer[3] = ~DELTA_FAN_DURATION_ONE_HOUR;
                        break;
                    case IR_DELTA_FAN_DURATION_2HR:
                        grmt_deltatimerBuffer[2] = DELTA_FAN_DURATION_TWO_HOUR;
                        grmt_deltatimerBuffer[3] = ~DELTA_FAN_DURATION_TWO_HOUR;
                        break;
                    case IR_DELTA_FAN_DURATION_3HR:
                        grmt_deltatimerBuffer[2] =
                            DELTA_FAN_DURATION_THREE_HOUR;
                        grmt_deltatimerBuffer[3] =
                            ~DELTA_FAN_DURATION_THREE_HOUR;
                        break;
                    case IR_DELTA_FAN_DURATION_4HR:
                        grmt_deltatimerBuffer[2] = DELTA_FAN_DURATION_FOUR_HOUR;
                        grmt_deltatimerBuffer[3] =
                            ~DELTA_FAN_DURATION_FOUR_HOUR;
                        break;
                    case IR_DELTA_FAN_DURATION_6HR:
                        grmt_deltatimerBuffer[2] = DELTA_FAN_DURATION_SIX_HOUR;
                        grmt_deltatimerBuffer[3] = ~DELTA_FAN_DURATION_SIX_HOUR;
                        break;
                    case IR_DELTA_FAN_DURATION_FOREVER:
                    default:
                        grmt_deltatimerBuffer[2] = DELTA_FAN_DURATION_FOREVER;
                        grmt_deltatimerBuffer[3] = ~DELTA_FAN_DURATION_FOREVER;
                        break;
                }
            }
            memcpy(rmt_msg->data, grmt_deltaBuffer, sizeof(grmt_deltaBuffer));
            memcpy(rmt_msg->time, grmt_deltatimerBuffer,
                   sizeof(grmt_deltatimerBuffer));
            break;
        case IR_TYPE_ZERO:
            if (rmt_msg->bstatusch)
            {
                grmt_zroBuffer[0] = 0x76;
                grmt_zroBuffer[1] = 0x80;
                memcpy(rmt_msg->data, grmt_zroBuffer, sizeof(grmt_zroBuffer));
            }

            if (rmt_msg->bfanspeedch)
            {
                if (rmt_msg->fanspeed > 0)
                {
                    grmt_zroBuffer[0] = 0x76;
                    grmt_zroBuffer[1] = 0x40;
                }
                if (rmt_msg->fanspeed < 0)
                {
                    grmt_zroBuffer[0] = 0x76;
                    grmt_zroBuffer[1] = 0x62;
                }
                memcpy(rmt_msg->data, grmt_zroBuffer, sizeof(grmt_zroBuffer));
            }

            if (rmt_msg->bswingch)
            {
                grmt_zroBuffer[0] = 0x76;
                grmt_zroBuffer[1] = 0xc2;
                memcpy(rmt_msg->data, grmt_zroBuffer, sizeof(grmt_zroBuffer));
            }
            break;
        default:
            break;
    }

    return SYSTEM_ERROR_NONE;
}

int ir_zerofan_tigger(rmt_zftg_msg_t msg)
{
    rmt_msg_t rmt_msg;
    int idx = 0, repeat = 0;
    memset(&rmt_msg, 0, sizeof(rmt_msg_t));
#if 0
    dbg_printf("\n Zero Tigger Message:\n");
    dbg_printf("   Active:%d, %d\n",msg.bactivech,msg.active);
    dbg_printf("    Speed:%d, %d\n",msg.bfanspeedch,msg.fanspeed);
    dbg_printf("    Swing:%d, %d\n\n",msg.bswingch,msg.swing);
#endif
    if (gsemaRmtZeroTig == NULL)
    {
        return SYSTEM_ERROR_NOT_READY;
    }

    if (xSemaphoreTake(gsemaRmtZeroTig, portMAX_DELAY) == pdTRUE)
    {
        rmt_msg.type = IR_TYPE_ZERO;
        if (msg.bactivech)
        {
            rmt_msg.bstatusch = true;
            rmt_msg.targetfreq = MAX9814_ZERO_FAN_BEE_FREQ;
            rmt_msg.pwrthreshold = MAX9814_ZERO_FAN_BEE_THRESHOLD;
            /* SendIR */
            if (!rmt_enqueue_msg(&rmt_msg))
            {
                xSemaphoreGive(gsemaRmtZeroTig);
                syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_ERROR,
                               "EnQueue Zero Fan IR active fail");
                return SYSTEM_ERROR_NOT_READY;
            }
            syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                           "EnQueue Zero Fan IR %d active", rmt_msg.type);
        }

        if (msg.bfanspeedch)
        {
            rmt_msg.bfanspeedch = true;
            if (msg.fanspeed > 0)
            {
                rmt_msg.fanspeed = 1;
            }
            if (msg.fanspeed < 0)
            {
                rmt_msg.fanspeed = -1;
            }
            rmt_msg.targetfreq = MAX9814_ZERO_FAN_BEE_FREQ;
            rmt_msg.pwrthreshold = MAX9814_ZERO_FAN_BEE_THRESHOLD;
            repeat = abs(msg.fanspeed);
            /* SendIR */
            for (idx = 0; idx < repeat; idx++)
            {
                if (!rmt_enqueue_msg(&rmt_msg))
                {
                    xSemaphoreGive(gsemaRmtZeroTig);
                    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_ERROR,
                                   "EnQueue Zero Fan IR speed fail in %d/%d",
                                   idx + 1, repeat);
                    return SYSTEM_ERROR_NOT_READY;
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
            syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                           "EnQueue Zero Fan IR speed %d times", repeat);
        }

        if (msg.bswingch)
        {
            rmt_msg.bswingch = true;
            rmt_msg.targetfreq = MAX9814_ZERO_FAN_BEE_FREQ;
            rmt_msg.pwrthreshold = MAX9814_ZERO_FAN_BEE_THRESHOLD;
            /* SendIR */
            if (!rmt_enqueue_msg(&rmt_msg))
            {
                xSemaphoreGive(gsemaRmtZeroTig);
                syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_ERROR,
                               "EnQueue Zero Fan IR swing fail");
                return SYSTEM_ERROR_NOT_READY;
            }
            syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                           "EnQueue Zero Fan IR %d swing", rmt_msg.type);
        }
        xSemaphoreGive(gsemaRmtZeroTig);
    }
    /* Success */
    return SYSTEM_ERROR_NONE;
}

int ir_deltafan_tigger(int mode, int active, int duration)
{
    int running_mode = IR_DELTA_FAN_TIGGER_MODE_MAX;
    int i = 0, repeat = 0;
    rmt_msg_t rmt_msg;

    memset(&rmt_msg, 0, sizeof(rmt_msg_t));
    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG, "Tigger Delta IR");
    if (gsemaRmtDeltaSche == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (rmt %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }

    if (xSemaphoreTake(gsemaRmtDeltaSche, portMAX_DELAY) == pdTRUE)
    {
        for (i = 0; i < IR_DELTA_FAN_TIGGER_MODE_MAX; i++)
        {
            if (active == IR_DELTA_FAN_TIGGER_ACTIVE_ON)
            {
                if (i < mode)
                {
                    if (grmt_deltascheduler[i] > DELTA_FAN_SCHDULER_IDEL)
                    {
                        /* High priority mode is active, set current mode to
                         * pending, keep current mode */
                        grmt_deltascheduler[mode] = DELTA_FAN_SCHDULER_PENDING;
                        syslog_handler(
                            SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                            "Scheduler pend %s, because Hier Pri FAN %s ON",
                            grmt_deltaschedulerName[mode],
                            grmt_deltaschedulerName[i]);
                        break;
                    }
                }
                else if (i == mode)
                {
                    /* Current mode is highest mode and active */
                    grmt_deltascheduler[mode] = DELTA_FAN_SCHDULER_ACTIVE;
                    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                                   "Scheduler active %s",
                                   grmt_deltaschedulerName[mode]);
                    running_mode = mode;
                }
                else
                {
                    /* Set lower mode from active to pending */
                    if (grmt_deltascheduler[i] == DELTA_FAN_SCHDULER_ACTIVE)
                    {
                        grmt_deltascheduler[i] = DELTA_FAN_SCHDULER_PENDING;
                        syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                                       "Scheduler pend Lower Pri FAN %s",
                                       grmt_deltaschedulerName[i]);
                    }
                }
            }

            if (active == IR_DELTA_FAN_TIGGER_ACTIVE_OFF)
            {
                if (i < mode)
                {
                    if (grmt_deltascheduler[i] > DELTA_FAN_SCHDULER_IDEL)
                    {
                        /* High priority mode is active or pending, Do nothing
                         */
                        /* Set current mode to idel */
                        grmt_deltascheduler[mode] = DELTA_FAN_SCHDULER_IDEL;
                        syslog_handler(
                            SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                            "Scheduler idel %s and keep Hier pri Fan %s ON",
                            grmt_deltaschedulerName[mode],
                            grmt_deltaschedulerName[i]);
                        running_mode = i;
                        break;
                    }
                }
                else if (i == mode)
                {
                    /* Set current mode to idel */
                    grmt_deltascheduler[mode] = DELTA_FAN_SCHDULER_IDEL;
                    syslog_handler(
                        SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                        "Scheduler idel %s then look for lower pend Fan",
                        grmt_deltaschedulerName[mode]);
                }
                else
                {
                    if (grmt_deltascheduler[i] > DELTA_FAN_SCHDULER_IDEL)
                    {
                        /* Active the next high mode */
                        grmt_deltascheduler[i] = DELTA_FAN_SCHDULER_ACTIVE;
                        syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                                       "Active next high pri %s",
                                       grmt_deltaschedulerName[i]);
                        running_mode = i;
                        break;
                    }
                }
                /* There isn't other pending mode, turn off fan */
                running_mode = IR_DELTA_FAN_TIGGER_MODE_OFF;
            }
        }

        syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                       "Scheduler result: %s",
                       grmt_deltaschedulerName[running_mode]);
        if (running_mode < IR_DELTA_FAN_TIGGER_MODE_MAX)
        {
            rmt_msg.bmodech = true;
            rmt_msg.mode = running_mode;
            rmt_msg.bdurationch = true;
            rmt_msg.duration = duration;

            // Sync to homekit
            ir_update_deltafan_status(rmt_ismanualfanactive(), 0);

            /* TODO 1CE3 is delta_swing */

            // Manual mode doesn't have to send IR again
            if (mode != IR_DELTA_FAN_TIGGER_MODE_MANUAL)
            {
                rmt_msg.type = IR_TYPE_DELTA;
                rmt_msg.repeat = repeat;
                rmt_msg.targetfreq = MAX9814_DELTA_FAN_BEE_FREQ;
                rmt_msg.pwrthreshold = MAX9814_DELTA_FAN_BEE_THRESHOLD;
                /* SendIR */
                if (!rmt_enqueue_msg(&rmt_msg))
                {
                    xSemaphoreGive(gsemaRmtDeltaSche);
                    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_ERROR,
                                   "EnQueue Delta IR fail");
                    return SYSTEM_ERROR_NOT_READY;
                }
                syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_DEBUG,
                               "EnQueue Delta IR %d", rmt_msg.type);
            }
        }
        xSemaphoreGive(gsemaRmtDeltaSche);
    }
    return true;
}

int ir_get_deltascheduler(int index, uint8_t *sheduler)
{
    if (gsemaRmtDeltaSche == NULL)
    {
        return SYSTEM_ERROR_NOT_READY;
    }
    if (sheduler == NULL)
    {
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaRmtDeltaSche, portMAX_DELAY) == pdTRUE)
    {
        *sheduler = grmt_deltascheduler[index];
        xSemaphoreGive(gsemaRmtDeltaSche);
    }
    return SYSTEM_ERROR_NONE;
}

int ir_set_deltascheduler(int index, uint8_t scheduler)
{
    if (gsemaRmtDeltaSche == NULL)
    {
        return SYSTEM_ERROR_NOT_READY;
    }

    if (xSemaphoreTake(gsemaRmtDeltaSche, portMAX_DELAY) == pdTRUE)
    {
        grmt_deltascheduler[index] = scheduler;
        xSemaphoreGive(gsemaRmtDeltaSche);
    }
    return SYSTEM_ERROR_NONE;
}

void ir_hitachiac_delay_timer_callback()
{
    rmt_hattg_msg_t tghitmsg;
    int ori_actype = 0, ori_acspeed = 0;

    memset(&tghitmsg, 0, sizeof(rmt_hattg_msg_t));

    tghitmsg.bactivech = true;
    tghitmsg.active = HAP_ACTIVE_OFF;

    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_INFO,
                   "Hitachi fan only timeout");
    ir_hitachiac_tigger(tghitmsg);
    return;
}

void ir_sync_delta_manual_timer_callback()
{
    // Sync Delta fan status to scheduler
    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_INFO,
                   "Timeout, turn off fan (manual)");
    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_MANUAL,
                       IR_DELTA_FAN_TIGGER_ACTIVE_OFF,
                       IR_DELTA_FAN_DURATION_FOREVER);
    return;
}
static void rmt_restart_receive(rmt_channel_handle_t channel,
                                rmt_symbol_word_t *symbols, size_t symbols_size,
                                const rmt_receive_config_t *config)
{
    esp_err_t err = rmt_receive(channel, symbols, symbols_size, config);
    if (err == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG_IR, "RX channel not ready, skip restart");
        return;
    }
    ESP_ERROR_CHECK(err);
}
