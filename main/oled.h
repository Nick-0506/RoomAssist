/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "ssd1306.h"
#include "qrcodegen.h"

#define LED_DISPLAY_MODE_OFF            0
#define LED_DISPLAY_MODE_TIME           1
#define LED_DISPLAY_MODE_SNOOZE         2
#define LED_DISPLAY_MODE_FWUG           3
#define LED_DISPLAY_MODE_WIFI_PROV      4
#define LED_DISPLAY_MODE_HOMEKIT_PAIR   5

#define LED_DISPLAY_TIME    30 /* Seconds */
#define LED_SNOOZE_TIME     30 /* Seconds */

#define OLED_NVS_NAMESPACE      "oled"
#define OLED_NVS_DISPLAY_KEY    "leddisplay"
#define OLED_NVS_SNOOZE_KEY     "ledsnooze"

void led_display_app_timer_callback();
void oled_saveconfig(char *key, int32_t data);
void draw_qrcode_to_oled(SSD1306_t * dev, const uint8_t *qrcode, int size);
bool generate_qrcode_data(const char *text, uint8_t *qrcode_out, size_t *size_out);

#ifdef __cplusplus
extern "C" {
#endif
int oled_getDisplayMode(int *);
int oled_setDisplayMode(int );
int oled_getDisplayTime(int *);
int oled_setDisplayTime(int );
int oled_getSnoozeTime(int *);
int oled_setSnoozeTime(int );
void task_oled(void *pvParameter);
#ifdef __cplusplus
}
#endif