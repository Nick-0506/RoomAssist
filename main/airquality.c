/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "airquality.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "syslog.h"
#include "system.h"
#include "homekit.h"
#include <app_hap_setup_payload.h>

// Drivers & Components
#include "rmt.h"
#include "ir_delta_encoder.h"
#include "mq135.h"
#include "dht22.h"
#if CONFIG_SGP41_ENABLE
#include "sgp41.h"
#endif

static const char *TAG_AQ = "airquality";

// --- Configuration Constants (Moved from mq135.h/c) ---
#define AIRQUALITY_NVS_NAMESPACE \
    "mq135"  // Keep using mq135 namespace for compat
#define KEY_VOC_HIGH "mq135thrh"
#define KEY_VOC_LOW "mq135thrl"
#define KEY_NOX_HIGH "sgp41noxthrh"
#define KEY_NOX_LOW "sgp41noxthrl"

// Timing
#define AQ_WARMTIME 20000

// --- Global Variables (Protected by Semaphore) ---
// Note: We use one semaphore for all AQ configuration
static SemaphoreHandle_t s_sema_aq_cfg = NULL;

static int s_cur_voc = 0;
static int s_cur_nox = 0;

// Defaults
static int s_voc_high = 80;
static int s_voc_low = 60;
static int s_nox_high = 3;
static int s_nox_low = 1;

// Timer handle for Fan
static esp_timer_handle_t s_fan_timer_handle = NULL;

// --- Helper Prototypes ---
static void airquality_load_nvs(void);
static void airquality_save_nvs(const char *key, int value);
static void ir_sync_delta_exhaust_timer_callback(void *arg);

// --- API Implementation ---

int airquality_get_voc_index(int *value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        *value = s_cur_voc;
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_set_voc_index(int value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        s_cur_voc = value;
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_get_nox_index(int *value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        *value = s_cur_nox;
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_set_nox_index(int value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        s_cur_nox = value;
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

// --- Thresholds ---

int airquality_get_voc_threshold_high(int *value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        *value = s_voc_high;
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_set_voc_threshold_high(int value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        s_voc_high = value;
        airquality_save_nvs(KEY_VOC_HIGH, value);
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_get_voc_threshold_low(int *value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        *value = s_voc_low;
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_set_voc_threshold_low(int value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        s_voc_low = value;
        airquality_save_nvs(KEY_VOC_LOW, value);
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_get_nox_threshold_high(int *value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        *value = s_nox_high;
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_set_nox_threshold_high(int value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        s_nox_high = value;
        airquality_save_nvs(KEY_NOX_HIGH, value);
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_get_nox_threshold_low(int *value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        *value = s_nox_low;
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

int airquality_set_nox_threshold_low(int value)
{
    if (!s_sema_aq_cfg) return ESP_FAIL;
    if (xSemaphoreTake(s_sema_aq_cfg, portMAX_DELAY) == pdTRUE)
    {
        s_nox_low = value;
        airquality_save_nvs(KEY_NOX_LOW, value);
        xSemaphoreGive(s_sema_aq_cfg);
        return ESP_OK;
    }
    return ESP_FAIL;
}

void airquality_reset_baseline(void)
{
#if CONFIG_SGP41_ENABLE
    sgp41_reset_baseline();
    syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                   "AirQuality: SGP41 Baseline Reset Requested");
#endif
}

// --- NVS Logic ---

static void airquality_load_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(AIRQUALITY_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return;

    int32_t val = 0;
    if (nvs_get_i32(handle, KEY_VOC_HIGH, &val) == ESP_OK) s_voc_high = val;
    if (nvs_get_i32(handle, KEY_VOC_LOW, &val) == ESP_OK) s_voc_low = val;
    if (nvs_get_i32(handle, KEY_NOX_HIGH, &val) == ESP_OK) s_nox_high = val;
    if (nvs_get_i32(handle, KEY_NOX_LOW, &val) == ESP_OK) s_nox_low = val;

    nvs_close(handle);
}

static void airquality_save_nvs(const char *key, int value)
{
    nvs_handle_t handle;
    if (nvs_open(AIRQUALITY_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_set_i32(handle, key, value);
        nvs_commit(handle);
        nvs_close(handle);
        syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                       "Config saved %s: %d", key, value);
    }
}

// --- Fan Control Logic ---

static void ir_sync_delta_exhaust_timer_callback(void *arg)
{
    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_INFO,
                   "Timeout, turn off exhust fan");
    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_EXHAUST,
                       IR_DELTA_FAN_TIGGER_ACTIVE_OFF,
                       IR_DELTA_FAN_DURATION_FOREVER);
    rmt_setexhaustfanstatus(false);
}

// --- Task Implementation ---

void task_airquality(void *arg)
{
    system_task_creating(TASK_MQ135_ID);

    uint8_t sys_mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));

    // Semaphore
    s_sema_aq_cfg = xSemaphoreCreateBinary();
    xSemaphoreGive(s_sema_aq_cfg);

    airquality_load_nvs();

    // Timer Init
    esp_timer_create_args_t timer_args = {
        .callback = &ir_sync_delta_exhaust_timer_callback, .name = "fan_timer"};

    // System Ready Checks
    syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                   "AirQuality Task Started");

    // Sensor Init
#if CONFIG_SGP41_ENABLE
    esp_err_t ret = sgp41_init();
    if (ret != ESP_OK)
    {
        syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_ERROR,
                       "SGP41 Init Failed: %s", esp_err_to_name(ret));
    }
#else
    // Init legacy MQ135 ADC
    mq135_adc_init();
#endif

    // Warmup
    system_task_created(TASK_MQ135_ID);
    vTaskDelay(pdMS_TO_TICKS(AQ_WARMTIME));

    bool sensor_error = false;
    int keeping_counter = 0;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(AQ_IDLETIME));

        int voc_idx = 0;
        int nox_idx = 0;
        bool valid_read = false;

        // Get Thresholds early for Gating (SGP41) and Control Logic
        int th_voc_h = 100, th_voc_l = 80, th_nox_h = 3, th_nox_l = 1;
        airquality_get_voc_threshold_high(&th_voc_h);
        airquality_get_voc_threshold_low(&th_voc_l);
        airquality_get_nox_threshold_high(&th_nox_h);
        airquality_get_nox_threshold_low(&th_nox_l);

#if CONFIG_SGP41_ENABLE
        // ---------------- SGP41 Measurement ----------------
        // Fetch Environment Data from DHT22 (Best Effort)
        sgp41_environment_t env;
        bool env_valid = false;
        float t = 0, h = 0;
        if (dht22_getcurrenttemperature(&t) == ESP_OK &&
            dht22_getcurrenthumidity(&h) == ESP_OK)
        {
            env.temperature_c = t;
            env.humidity_rh = h;
            env_valid = true;
        }

        sgp41_measurement_t meas = {0};
        if (sgp41_measure(env_valid ? &env : NULL, &meas, th_voc_h, th_voc_l,
                          th_nox_h, th_nox_l) == ESP_OK)
        {
            voc_idx = meas.voc_index;
            nox_idx = meas.nox_index;
            valid_read = true;
            if (sensor_error)
            {
                syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                               "Sensor Back Normal");
                sensor_error = false;
            }
        }
        else
        {
            if (!sensor_error)
            {
                syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_ERROR,
                               "SGP41 Read Failed");
                sensor_error = true;
            }
            airquality_set_voc_index(-1);
            airquality_set_nox_index(-1);
        }
#else
        // ---------------- Legacy MQ135 ADC ----------------
        int aqi = 0;
        if (mq135_read_aqi(&aqi) == ESP_OK)
        {
            voc_idx = aqi;
            nox_idx = 0;
            valid_read = true;
        }
#endif

        if (valid_read)
        {
            // Update Global State
            airquality_set_voc_index(voc_idx);
#if CONFIG_SGP41_ENABLE
            airquality_set_nox_index(nox_idx);
#endif

            // Update HomeKit
            int hap_val =
                (((voc_idx - 1) / 50 + 1) < 6 ? ((voc_idx - 1) / 50 + 1) : 5);
            hap_update_value(HAP_ACCESSORY_AIRQUALITY, HAP_CHARACTER_IGNORE,
                             (void *)&hap_val);

            // ---------------- Control Logic ----------------
            // Note: BATHROOM/SAMPLE check is done at task creation level in
            // app_main.c So we execute logic directly here.

            // logic execution

            bool trigger_voc = (voc_idx >= th_voc_h);
            bool trigger_nox = (CONFIG_SGP41_ENABLE && (nox_idx >= th_nox_h));

            if (trigger_voc || trigger_nox)
            {
                if (!rmt_isexhaustfanactive())
                {
                    uint64_t duration = ((uint64_t)IR_DELTA_FAN_DURATION_1HR) *
                                        60 * 60 * 1000 * 1000;

                    syslog_handler(
                        SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                        "ALARM! VOC=%d(Limit %d) NOx=%d(Limit %d). Fan ON.",
                        voc_idx, th_voc_h, nox_idx, th_nox_h);

                    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_EXHAUST,
                                       IR_DELTA_FAN_TIGGER_ACTIVE_ON,
                                       IR_DELTA_FAN_DURATION_1HR);
                    rmt_setexhaustfanstatus(true);

                    // Start Timer
                    if (s_fan_timer_handle == NULL)
                    {
                        esp_timer_create(&timer_args, &s_fan_timer_handle);
                    }
                    if (esp_timer_is_active(s_fan_timer_handle))
                    {
                        esp_timer_stop(s_fan_timer_handle);
                    }
                    esp_timer_start_once(s_fan_timer_handle, duration);
                }
                keeping_counter = 0;  // Reset off-delay
            }
            else if (rmt_isexhaustfanactive())
            {
                // Check OFF conditions (Hysteresis)
                bool clear_voc = (voc_idx < th_voc_l);
                bool clear_nox = (!CONFIG_SGP41_ENABLE || (nox_idx < th_nox_l));

                if (clear_voc && clear_nox)
                {
                    // Delay Logic (Keeping)
                    // 5 minutes delay (300000 ms / IDLETIME)
                    if (keeping_counter > (300000 / AQ_IDLETIME))
                    {
                        syslog_handler(SYSLOG_FACILITY_AIRQUALITY,
                                       SYSLOG_LEVEL_INFO,
                                       "Air Clean. VOC=%d NOx=%d. Fan OFF.",
                                       voc_idx, nox_idx);

                        ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_EXHAUST,
                                           IR_DELTA_FAN_TIGGER_ACTIVE_OFF,
                                           IR_DELTA_FAN_DURATION_1HR);
                        rmt_setexhaustfanstatus(false);

                        if (s_fan_timer_handle &&
                            esp_timer_is_active(s_fan_timer_handle))
                        {
                            esp_timer_stop(s_fan_timer_handle);
                        }
                    }
                    else
                    {
                        keeping_counter++;
                    }
                }
                else
                {
                    keeping_counter = 0;  // Reset if signals bounce back up
                }
            }
        }
    }
}
