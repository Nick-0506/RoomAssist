/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sgp41.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "syslog.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define TAG_SGP41 "sgp41"

// Dual I2C Architecture: OLED on Port 0, SGP41 on Port 1
#define SGP41_I2C_PORT I2C_NUM_1
#define SGP41_SDA_PIN 25
#define SGP41_SCL_PIN 26
#define SGP41_I2C_FREQ_HZ 100000

#define SGP41_I2C_ADDRESS 0x59
#define SGP41_CMD_SOFT_RESET 0x0006
#define SGP41_CMD_EXECUTE_CONDITIONING 0x2612
#define SGP41_CMD_MEASURE_RAW 0x2619

#define SGP41_I2C_TIMEOUT_MS 100
#define SGP41_CONDITIONING_DELAY_MS 50
#define SGP41_MEASURE_DELAY_MS 80
#define SGP41_DEFAULT_TEMP_C 25.0f
#define SGP41_DEFAULT_HUMI_RH 50.0f
// SGP41 Specific Parameters
#include "airquality.h"

#define SGP41_VOC_LEARNING_RATE     \
    ((float)AQ_IDLETIME / 1000.0f / \
     43200.0f)  // 12 Hours Time Constant (Sensirion Official)
#define SGP41_NOX_LEARNING_RATE ((float)AQ_IDLETIME / 1000.0f / 43200.0f)

// ABC / NVS Parameters
#define SGP41_NVS_NAMESPACE "sgp41_base"
#define SGP41_NVS_KEY_VOC_BASE "voc_base"
#define SGP41_NVS_KEY_NOX_BASE "nox_base"
#define SGP41_HOURLY_CHECK_US (3600LL * 1000LL * 1000LL)  // 1 Hour
#define SGP41_NVS_SAVE_THRESHOLD 100.0f  // Only save if diff > 100 ticks

// Startup State Machine
typedef enum
{
    SGP41_STATE_CONDITIONING,   // 0-60s: Hardware stabilization
    SGP41_STATE_FAST_LEARNING,  // 60s-5min: Rapid baseline adjustment
    SGP41_STATE_NORMAL          // 5min+: Normal operation
} sgp41_state_t;

#define SGP41_CONDITIONING_DURATION_US (60LL * 1000LL * 1000LL)  // 60 seconds
#define SGP41_FAST_LEARNING_DURATION_US \
    (5LL * 60LL * 1000LL * 1000LL)                 // 5 minutes
#define SGP41_BASELINE_SANITY_THRESHOLD 5000.0f    // Ticks deviation threshold
#define SGP41_BASELINE_WARNING_THRESHOLD 10000.0f  // Warning threshold

static SemaphoreHandle_t s_sgp41_lock;
static bool s_initialized;
static int64_t s_conditioning_deadline_us;

// State machine variables
static sgp41_state_t s_current_state = SGP41_STATE_CONDITIONING;
static int64_t s_state_start_time = 0;
static int s_measurement_count = 0;

// Baseline variables (Runtime)
static float s_voc_baseline_ticks;
static float s_nox_baseline_ticks;

// Gating State (Hysteresis)
static bool s_voc_is_gated = false;
static bool s_nox_is_gated = false;

// NVS periodic check timestamp
static int64_t s_last_nvs_check_timestamp;

// Debug log throttling (15 minutes)
static int64_t s_last_debug_log_timestamp;
#define SGP41_DEBUG_LOG_INTERVAL_US \
    (15LL * 60LL * 1000LL * 1000LL)  // 15 minutes

// Variables to track what is currently in NVS (to minimize writes)
static float s_nvs_saved_voc_base;
static float s_nvs_saved_nox_base;

// Dynamic Sensitivity (exposed via setter)
static float s_voc_sensitivity = 35.0f;
static float s_nox_sensitivity = 5.0f;  // High Sensitivity (Standard is ~45.0f)

void sgp41_set_voc_sensitivity(float val) { s_voc_sensitivity = val; }
void sgp41_set_nox_sensitivity(float val) { s_nox_sensitivity = val; }

// NVS Helper Functions
static void sgp41_load_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(SGP41_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK)
    {
        size_t size = sizeof(float);
        nvs_get_blob(handle, SGP41_NVS_KEY_VOC_BASE, &s_voc_baseline_ticks,
                     &size);
        nvs_get_blob(handle, SGP41_NVS_KEY_NOX_BASE, &s_nox_baseline_ticks,
                     &size);

        nvs_close(handle);

        s_nvs_saved_voc_base = s_voc_baseline_ticks;
        s_nvs_saved_nox_base = s_nox_baseline_ticks;

        syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                       "SGP41: Loaded Baseline from NVS. VOC=%.1f, NOx=%.1f",
                       s_voc_baseline_ticks, s_nox_baseline_ticks);
    }
    else
    {
        syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_WARNING,
                       "SGP41: No NVS Baseline found, starting fresh.");
    }
}

static void sgp41_save_nvs(bool force)
{
    bool need_save = force;
    if (fabs(s_voc_baseline_ticks - s_nvs_saved_voc_base) >
        SGP41_NVS_SAVE_THRESHOLD)
        need_save = true;
    if (fabs(s_nox_baseline_ticks - s_nvs_saved_nox_base) >
        SGP41_NVS_SAVE_THRESHOLD)
        need_save = true;

    if (need_save)
    {
        nvs_handle_t handle;
        if (nvs_open(SGP41_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
        {
            nvs_set_blob(handle, SGP41_NVS_KEY_VOC_BASE, &s_voc_baseline_ticks,
                         sizeof(float));
            nvs_set_blob(handle, SGP41_NVS_KEY_NOX_BASE, &s_nox_baseline_ticks,
                         sizeof(float));

            nvs_commit(handle);
            nvs_close(handle);

            s_nvs_saved_voc_base = s_voc_baseline_ticks;
            s_nvs_saved_nox_base = s_nox_baseline_ticks;
        }
    }
}

static uint8_t sgp41_crc(uint8_t msb, uint8_t lsb)
{
    uint8_t crc = 0xFF;
    uint8_t data[2] = {msb, lsb};

    for (int b = 0; b < 2; b++)
    {
        crc ^= data[b];
        for (int i = 0; i < 8; i++)
        {
            if ((crc & 0x80) != 0)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static int sgp41_compute_index(uint16_t sample, float *baseline,
                               float learning_rate, float sensitivity,
                               float offset_val, int gating_high,
                               int gating_low, bool *is_gated)
{
    // 1. Calculate Tentative Index (Pre-Update)
    float delta_check = *baseline - (float)sample;
    float normalized_check = offset_val + (delta_check / sensitivity);
    if (normalized_check < 0.0f) normalized_check = 0.0f;
    if (normalized_check > 500.0f) normalized_check = 500.0f;
    int tentative_index = (int)lroundf(normalized_check);

    // 2. Update Gating State (Hysteresis)
    // Lock if >= High (User Alarm Limit)
    // Unlock if < Low (User Alarm Clear Limit)
    // If no thresholds provided (e.g. 0), disable gating
    if (gating_high > 0)
    {
        if (!(*is_gated))
        {
            if (tentative_index >= gating_high) *is_gated = true;
        }
        else
        {
            if (tentative_index < gating_low) *is_gated = false;
        }
    }

    // 3. Baseline Update Logic
    // ONLY update baseline if NOT Gated (Pollution)
    // EXCEPTION: If we find Cleaner Air (sample > baseline), ALWAYS update
    // (Accel Mode)

    if (*baseline <= 0.0f)
    {
        *baseline = sample;
    }
    else
    {
        if (sample > *baseline)
        {
            // Found cleaner air, update baseline quickly (Accel Mode)
            // Even if gated (polluted), finding cleaner air means our baseline
            // was wrong.
            *baseline = sample;
        }
        else
        {
            // Normal Learning: Only if NOT Gated
            if (!(*is_gated))
            {
                *baseline += ((float)sample - *baseline) * learning_rate;
            }
            // Else: Gating Active. Do NOT decay baseline.
        }
    }

    // 4. Final Calculation
    float delta = *baseline - (float)sample;
    float normalized = offset_val + (delta / sensitivity);

    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 500.0f) normalized = 500.0f;
    return (int)lroundf(normalized);
}

void sgp41_deinit(void)
{
    s_initialized = false;
    i2c_driver_delete(SGP41_I2C_PORT);
    printf("sgp41: Driver de-initialized\n");
}

static esp_err_t sgp41_i2c_init(void)
{
    ESP_LOGI(TAG_SGP41, "Initializing I2C Port %d for SGP41 (SDA=%d, SCL=%d)",
             SGP41_I2C_PORT, SGP41_SDA_PIN, SGP41_SCL_PIN);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SGP41_SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = SGP41_SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = SGP41_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(SGP41_I2C_PORT, &conf);
    if (err != ESP_OK) return err;

    return i2c_driver_install(SGP41_I2C_PORT, conf.mode, 0, 0, 0);
}

esp_err_t sgp41_init(void)
{
    printf("SGP41 Driver Version: ABC-002 (Dual Bus: Port 1, Pins 25/26)\n");

    if (s_sgp41_lock == NULL)
    {
        s_sgp41_lock = xSemaphoreCreateMutex();
    }

    // Initialize I2C Driver explicitly for Port 1
    esp_err_t ret = sgp41_i2c_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_SGP41, "Failed to init I2C: %s", esp_err_to_name(ret));
        // Try to continue if already initialized? or fail?
        // If it fails with INVALID_STATE, it might be already inited.
        // But we should report it.
        if (ret != ESP_ERR_INVALID_STATE)
        {
            return ret;
        }
    }

    s_conditioning_deadline_us = 0;

    // Initialize state machine
    s_current_state = SGP41_STATE_CONDITIONING;
    s_state_start_time = esp_timer_get_time();
    s_measurement_count = 0;

    // Load from NVS
    sgp41_load_nvs();

    s_last_nvs_check_timestamp = esp_timer_get_time();

    s_initialized = true;
    return ESP_OK;
}

static esp_err_t write_read_measurement(uint16_t command, uint16_t arg_comp_rh,
                                        uint16_t arg_comp_t, uint8_t *out_data,
                                        size_t out_len)
{
    uint8_t cmd_data[8];
    cmd_data[0] = (uint8_t)(command >> 8);
    cmd_data[1] = (uint8_t)(command & 0xFF);
    cmd_data[2] = (uint8_t)(arg_comp_rh >> 8);
    cmd_data[3] = (uint8_t)(arg_comp_rh & 0xFF);
    cmd_data[4] = sgp41_crc(cmd_data[2], cmd_data[3]);
    cmd_data[5] = (uint8_t)(arg_comp_t >> 8);
    cmd_data[6] = (uint8_t)(arg_comp_t & 0xFF);
    cmd_data[7] = sgp41_crc(cmd_data[5], cmd_data[6]);

    esp_err_t ret =
        i2c_master_write_to_device(SGP41_I2C_PORT, SGP41_I2C_ADDRESS, cmd_data,
                                   8, pdMS_TO_TICKS(SGP41_I2C_TIMEOUT_MS));
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(SGP41_MEASURE_DELAY_MS));
    return i2c_master_read_from_device(SGP41_I2C_PORT, SGP41_I2C_ADDRESS,
                                       out_data, out_len,
                                       pdMS_TO_TICKS(SGP41_I2C_TIMEOUT_MS));
}

static esp_err_t sgp41_measure_locked(const sgp41_environment_t *env,
                                      sgp41_measurement_t *result,
                                      int gating_voc_high, int gating_voc_low,
                                      int gating_nox_high, int gating_nox_low)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret;
    int64_t now_us = esp_timer_get_time();
    bool conditioning_phase = false;

    // 1. Conditioning Phase Logic
    if (s_conditioning_deadline_us == 0)
    {
        syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                       "SGP41: Starting Conditioning (60s)");
        s_conditioning_deadline_us = now_us + SGP41_CONDITIONING_DURATION_US;

        uint16_t default_rh = 0x8000;
        uint16_t default_t = 0x6666;

        uint8_t cmd_data[8];
        cmd_data[0] = 0x26;
        cmd_data[1] = 0x12;
        cmd_data[2] = (uint8_t)(default_rh >> 8);
        cmd_data[3] = (uint8_t)(default_rh & 0xFF);
        cmd_data[4] = sgp41_crc(cmd_data[2], cmd_data[3]);
        cmd_data[5] = (uint8_t)(default_t >> 8);
        cmd_data[6] = (uint8_t)(default_t & 0xFF);
        cmd_data[7] = sgp41_crc(cmd_data[5], cmd_data[6]);

        ret = i2c_master_write_to_device(SGP41_I2C_PORT, SGP41_I2C_ADDRESS,
                                         cmd_data, 8,
                                         pdMS_TO_TICKS(SGP41_I2C_TIMEOUT_MS));
        if (ret != ESP_OK)
        {
            s_conditioning_deadline_us = 0;
            ESP_LOGE(TAG_SGP41, "Conditioning I2C Error: %s",
                     esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(SGP41_CONDITIONING_DELAY_MS));
        conditioning_phase = true;
    }
    else if (now_us < s_conditioning_deadline_us)
    {
        conditioning_phase = true;
    }

    // 2. Measure Raw
    float temp_c = SGP41_DEFAULT_TEMP_C;
    float humi_rh = SGP41_DEFAULT_HUMI_RH;

    if (env != NULL)
    {
        temp_c = env->temperature_c;
        humi_rh = env->humidity_rh;
    }

    uint16_t comp_rh = (uint16_t)((humi_rh * 65535.0f) / 100.0f);
    uint16_t comp_t = (uint16_t)(((temp_c + 45.0f) * 65535.0f) / 175.0f);

    uint8_t read_buffer[6];
    ret = write_read_measurement(SGP41_CMD_MEASURE_RAW, comp_rh, comp_t,
                                 read_buffer, 6);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_SGP41, "Measure I2C Error: %s", esp_err_to_name(ret));
        return ret;
    }

    if (sgp41_crc(read_buffer[0], read_buffer[1]) != read_buffer[2])
    {
        ESP_LOGE(TAG_SGP41,
                 "CRC Mismatch VOC: 0x%02x%02x CRC=0x%02x Expected=0x%02x",
                 read_buffer[0], read_buffer[1], read_buffer[2],
                 sgp41_crc(read_buffer[0], read_buffer[1]));
        return ESP_ERR_INVALID_CRC;
    }
    if (sgp41_crc(read_buffer[3], read_buffer[4]) != read_buffer[5])
    {
        ESP_LOGE(TAG_SGP41,
                 "CRC Mismatch NOx: 0x%02x%02x CRC=0x%02x Expected=0x%02x",
                 read_buffer[3], read_buffer[4], read_buffer[5],
                 sgp41_crc(read_buffer[3], read_buffer[4]));
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t sraw_voc = (read_buffer[0] << 8) | read_buffer[1];
    uint16_t sraw_nox = (read_buffer[3] << 8) | read_buffer[4];

    /* syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_DEBUG,
        "SGP41 Raw Ticks: VOC=%u, NOx=%u", sraw_voc, sraw_nox); */

    sgp41_measurement_t measurement = {0};
    measurement.voc_ticks = sraw_voc;
    measurement.nox_ticks = sraw_nox;

    // 3. State Machine Transitions & Baseline Calibration
    if (!conditioning_phase)
    {
        s_measurement_count++;
        int64_t elapsed_us = now_us - s_state_start_time;

        // State transitions
        if (s_current_state == SGP41_STATE_CONDITIONING &&
            elapsed_us > SGP41_CONDITIONING_DURATION_US)
        {
            s_current_state = SGP41_STATE_FAST_LEARNING;
            syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                           "SGP41: Entering Fast Learning Phase (5min)");
        }
        else if (s_current_state == SGP41_STATE_FAST_LEARNING &&
                 elapsed_us > SGP41_FAST_LEARNING_DURATION_US)
        {
            s_current_state = SGP41_STATE_NORMAL;
            syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                           "SGP41: Entering Normal Operation");
        }

        // Baseline sanity check during Fast Learning
        if (s_current_state == SGP41_STATE_FAST_LEARNING)
        {
            float voc_deviation = fabsf(s_voc_baseline_ticks - (float)sraw_voc);
            float nox_deviation = fabsf(s_nox_baseline_ticks - (float)sraw_nox);

            if (voc_deviation > SGP41_BASELINE_SANITY_THRESHOLD)
            {
                syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_WARNING,
                               "SGP41: VOC Baseline deviation %.0f, forcing "
                               "reset to current Ticks %u",
                               voc_deviation, sraw_voc);
                s_voc_baseline_ticks = sraw_voc;
            }

            if (nox_deviation > SGP41_BASELINE_SANITY_THRESHOLD)
            {
                syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_WARNING,
                               "SGP41: NOx Baseline deviation %.0f, forcing "
                               "reset to current Ticks %u",
                               nox_deviation, sraw_nox);
                s_nox_baseline_ticks = sraw_nox;
            }
        }

        // Periodic baseline sanity check in Normal state
        if (s_current_state == SGP41_STATE_NORMAL)
        {
            float voc_deviation = fabsf(s_voc_baseline_ticks - (float)sraw_voc);
            float nox_deviation = fabsf(s_nox_baseline_ticks - (float)sraw_nox);

            if (voc_deviation > SGP41_BASELINE_WARNING_THRESHOLD)
            {
                syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_WARNING,
                               "SGP41: VOC Baseline deviation %.0f exceeds "
                               "warning threshold",
                               voc_deviation);
            }

            if (nox_deviation > SGP41_BASELINE_WARNING_THRESHOLD)
            {
                syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_WARNING,
                               "SGP41: NOx Baseline deviation %.0f exceeds "
                               "warning threshold",
                               nox_deviation);
            }
        }

        // Hourly NVS save check
        if ((now_us - s_last_nvs_check_timestamp) > SGP41_HOURLY_CHECK_US)
        {
            sgp41_save_nvs(false);
            s_last_nvs_check_timestamp = now_us;
        }

        // Conditional Gating and Learning Rate based on state
        int voc_gating_high = gating_voc_high;
        int voc_gating_low = gating_voc_low;
        int nox_gating_high = gating_nox_high;
        int nox_gating_low = gating_nox_low;
        float voc_learning_rate = SGP41_VOC_LEARNING_RATE;
        float nox_learning_rate = SGP41_NOX_LEARNING_RATE;

        // Disable Gating during Conditioning and Fast Learning
        if (s_current_state != SGP41_STATE_NORMAL)
        {
            voc_gating_high = 0;  // Disable gating
            voc_gating_low = 0;
            nox_gating_high = 0;
            nox_gating_low = 0;
        }

        // Use faster Learning Rate during Fast Learning (6h instead of 12h)
        if (s_current_state == SGP41_STATE_FAST_LEARNING)
        {
            voc_learning_rate = SGP41_VOC_LEARNING_RATE * 2.0f;
            nox_learning_rate = SGP41_NOX_LEARNING_RATE * 2.0f;
        }

        if (measurement.voc_ticks > 0)
        {
            measurement.voc_index = sgp41_compute_index(
                measurement.voc_ticks, &s_voc_baseline_ticks, voc_learning_rate,
                s_voc_sensitivity, 100.0f, voc_gating_high, voc_gating_low,
                &s_voc_is_gated);
        }
        else
        {
            measurement.voc_index = 0;
        }

        if (measurement.nox_ticks > 0)
        {
            measurement.nox_index = sgp41_compute_index(
                measurement.nox_ticks, &s_nox_baseline_ticks, nox_learning_rate,
                s_nox_sensitivity, 1.0f, nox_gating_high, nox_gating_low,
                &s_nox_is_gated);
        }
        else
        {
            measurement.nox_index = 1;
        }

        // Debug log throttling: only log every 15 minutes
        if ((now_us - s_last_debug_log_timestamp) > SGP41_DEBUG_LOG_INTERVAL_US)
        {
            syslog_handler(
                SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_DEBUG,
                "SGP41 VOC: Tick=%u Base=%.1f Idx=%d Sens=%.1f LR=%.6f",
                measurement.voc_ticks, s_voc_baseline_ticks,
                measurement.voc_index, s_voc_sensitivity,
                SGP41_VOC_LEARNING_RATE);

            syslog_handler(
                SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_DEBUG,
                "SGP41 NOx: Tick=%u Base=%.1f Idx=%d Sens=%.1f LR=%.6f",
                measurement.nox_ticks, s_nox_baseline_ticks,
                measurement.nox_index, s_nox_sensitivity,
                SGP41_NOX_LEARNING_RATE);

            s_last_debug_log_timestamp = now_us;
        }
    }
    else
    {
        measurement.voc_index = -1;
        measurement.nox_index = -1;
    }

    *result = measurement;
    return ESP_OK;
}

esp_err_t sgp41_measure(const sgp41_environment_t *env,
                        sgp41_measurement_t *result, int gating_voc_high,
                        int gating_voc_low, int gating_nox_high,
                        int gating_nox_low)
{
    if (s_sgp41_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_sgp41_lock, portMAX_DELAY);
    esp_err_t ret =
        sgp41_measure_locked(env, result, gating_voc_high, gating_voc_low,
                             gating_nox_high, gating_nox_low);
    xSemaphoreGive(s_sgp41_lock);
    return ret;
}

void sgp41_reset_baseline(void)
{
    s_voc_baseline_ticks = 0;
    s_nox_baseline_ticks = 0;
    sgp41_save_nvs(true);
}
