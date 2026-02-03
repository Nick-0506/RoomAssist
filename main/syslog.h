/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSLOG_NVS_NAMESPACE            "SYSLOGCFG"
#define SYSLOG_NVS_SERVER_IP            "serverip"
#define SYSLOG_MAXLEN_IP                15
#define SYSLOG_MAXLEN_FACILITY_STR      15
#define SYSLOG_MAXLEN_LEVEL_STR         10
#define SYSLOG_SERVER_PORT 8514

#define SYSLOG_FACILITY_AIRQUALITY      0
#define SYSLOG_FACILITY_ANN             1
#define SYSLOG_FACILITY_ELF             2
#define SYSLOG_FACILITY_HOMEKIT         3
#define SYSLOG_FACILITY_HTTP            4
#define SYSLOG_FACILITY_HUMIDITY        5
#define SYSLOG_FACILITY_IR              6
#define SYSLOG_FACILITY_MAX9814         7
#define SYSLOG_FACILITY_OCCUPANCY       8
#define SYSLOG_FACILITY_OLED            9
#define SYSLOG_FACILITY_OTA             10
#define SYSLOG_FACILITY_RMT             11
#define SYSLOG_FACILITY_SNTP            12
#define SYSLOG_FACILITY_SYSLOG          13
#define SYSLOG_FACILITY_SYSTEM          14
#define SYSLOG_FACILITY_TELNET          15
#define SYSLOG_FACILITY_TEMPERATURE     16
#define SYSLOG_FACILITY_THINGSPEAK      17
#define SYSLOG_FACILITY_WEB             18
#define SYSLOG_FACILITY_MAXNUM          SYSLOG_FACILITY_WEB+1

#define SYSLOG_LEVEL_DEBUG              0
#define SYSLOG_LEVEL_INFO               1
#define SYSLOG_LEVEL_WARNING            2
#define SYSLOG_LEVEL_ALERT              3
#define SYSLOG_LEVEL_ERROR              4
#define SYSLOG_LEVEL_MAXNUM             SYSLOG_LEVEL_ERROR+1

#define SYSLOG_LEVEL_DEFAULT            0x1E

#define SYSLOG_LEVEL_NDEBUG              "Debug"
#define SYSLOG_LEVEL_NINFO               "Info"
#define SYSLOG_LEVEL_NWARNING            "Warning"
#define SYSLOG_LEVEL_NALERT              "Alert"
#define SYSLOG_LEVEL_NERROR              "Error"

#define SYSLOG_CFG_PATH                  "/spiffs/slg_sw.bin"

extern char gsyslog_facility_str[SYSLOG_FACILITY_MAXNUM][SYSLOG_MAXLEN_FACILITY_STR+1];
extern char gsyslog_level_str[SYSLOG_LEVEL_MAXNUM][SYSLOG_MAXLEN_LEVEL_STR+1];
void syslog_handler(uint32_t facility, uint32_t level, const char *fmt, ...);
void syslog_restoreconfig(void);
void syslog_saveconfig(char *key, char *serverip);
int syslog_set_facility_level(uint32_t facility, uint32_t level);
int syslog_get_facility_level(uint32_t facility, uint32_t* level);
int syslog_set_server_ip(char *ip, int len);
int syslog_get_server_ip(char *ip, int len);
int syslog_set_level_status(uint32_t facility, uint32_t level, bool status);

#ifdef __cplusplus
}
#endif