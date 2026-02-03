/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
/* For Homekit */
#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>
#include <hap_fw_upgrade.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DYSON_FAN_DEFAULT_STATUS 0
#define DYSON_FAN_DEFAULT_SPEED 1
#define DYSON_FAN_DEFAULT_SWING 0

/* Non Bathroom: Aircontroller, ELD switch, Humidity sensor, Occupancy sensor,
   ZeroFan Bathroom:     Aircontroller, ELD switch, Humidity sensor, Temperature
   sensor, Occupancy sensor, DeltaFan */
#define IS_BATHROOM(sys_mac) (sys_mac[4] == 0xF0 || sys_mac[4] == 0xF7)
#define IS_SAMPLE(sys_mac) (sys_mac[4] == 0x8D)

/* HAP Task Info */
#define HAP_ACC_TASK_PRIORITY 1
#define HAP_ACC_TASK_STACKSIZE 4 * 1024
#define HAP_ACC_TASK_NAME "hap_emulator"
#define HAP_HTTP_TASK_NAME "http"

#define HAP_MAX_NAME_LENGTH 16
#define HAP_MAX_SERIESNUMBER_LENGTH 12

#define DELTAFAN_NVS_NAMESPACE "DELTAFANCFG"
#define DELTAFAN_NVS_STATUS_KEY "status"

// Configuration
#define ELF_NVS_NAMESPACE "ELFCFG"
#define ELF_NVS_STATUS_KEY "status"
#define ELF_NVS_OCCUPANCYFANSTATUS_KEY "fanstatus"

// Default value
#define ELF_DEFAULT_STATUS true

/* Reset network credentials if button is pressed for more than 3 seconds and
 * then released */
#define RESET_NETWORK_BUTTON_TIMEOUT 3

/* Reset to factory if button is pressed and held for more than 10 seconds */
#define RESET_TO_FACTORY_BUTTON_TIMEOUT 10
/* The button "Boot" will be used as the Reset button for the example */
#define RESET_GPIO GPIO_NUM_0

#define HAP_ACCESSORY_OCCUPANCY 1
#define HAP_ACCESSORY_HITACHI_AC 2
#define HAP_ACCESSORY_TEMPERATURE 3
#define HAP_ACCESSORY_HUMIDITY 4
#define HAP_ACCESSORY_ELF 5
#define HAP_ACCESSORY_DELTA_FAN 6
#define HAP_ACCESSORY_ZERO_FAN 7
#define HAP_ACCESSORY_AIRQUALITY 8

#define HAP_CHARACTER_IGNORE 0
#define HAP_CHARACTER_ACTIVE 1
#define HAP_CHARACTER_MODE 2
#define HAP_CHARACTER_FAN_SPEED 3
#define HAP_CHARACTER_FAN_SWING 4
#define HAP_CHARACTER_CUR_TEMPERATURE 5
#define HAP_CHARACTER_CUR_HUMIDITY 6
#define HAP_CHARACTER_HEATING_THRESHOLD 7
#define HAP_CHARACTER_COOLING_THRESHOLD 8

#define HAP_ACTIVE_OPERATION 9
#define HAP_ACTIVE_ON 1
#define HAP_ACTIVE_OFF 2
#define HAP_ACTIVE_PRE_OFF 0

#define HAP_AC_MODE_OPERATION 12
#define HAP_AC_MODE_AUTO 0
#define HAP_AC_MODE_HEATER 1
#define HAP_AC_MODE_COOLER 2

#define HAP_FAN_FAN_SPEED_OPERATION 11
#define HAP_FAN_FAN_SWING_OPERATION 12

#define HAP_AC_LOW_TEMP_OPERATION 13
#define HAP_AC_HIGH_TEMP_OPERATION 14
#define HAP_AC_FAN_SPEED_OPERATION 15
#define HAP_AC_FAN_SWING_OPERATION 16

  extern char gsetupcode[];  // oled

  void ac_saveconfig(char *key, int value);
  void task_homekit_init(void *p);
  void zerofan_saveconfig(char *key, int value);
  void deltafan_saveconfig(char *key, int value);
  void elf_saveconfig(char *key, int value);
  void elf_restoreconfig(void);
  int hap_setelfstatus(int);
  int hap_setelfoccupancyfanstatus(int);
  bool hap_iselfactive();
  bool hap_iselfoccupancyfanactive();
  int hap_update_value(int, int, void *);

#ifdef __cplusplus
}
#endif