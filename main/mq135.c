/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mq135.h"
#include "adc_helper.h"
#include "dht22.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"
#include "syslog.h"
#include "system.h"

static const char *TAG_MQ135 = "mq135";

// ADC Handles
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_adc_calibrated = false;

// Temp/Hum Correction Constants
#define MQ135_TEMP_CORR_A 0.00035f
#define MQ135_TEMP_CORR_B -0.02718f
#define MQ135_TEMP_CORR_C 1.39538f
#define MQ135_TEMP_CORR_REF_HUMIDITY 33.0f
#define MQ135_TEMP_HUMI_COEFF 0.0018f
#define MQ135_TEMP_CORR_MIN 0.5f
#define MQ135_TEMP_CORR_MAX 1.5f

// --- Helper Functions ---

static float mq135_compute_temperature_correction(float temperature,
                                                  float humidity)
{
  float correction =
      (MQ135_TEMP_CORR_A * temperature * temperature) +
      (MQ135_TEMP_CORR_B * temperature) + MQ135_TEMP_CORR_C -
      (humidity - MQ135_TEMP_CORR_REF_HUMIDITY) * MQ135_TEMP_HUMI_COEFF;

  if (correction < MQ135_TEMP_CORR_MIN)
    correction = MQ135_TEMP_CORR_MIN;
  else if (correction > MQ135_TEMP_CORR_MAX)
    correction = MQ135_TEMP_CORR_MAX;

  return correction;
}

static bool mq135_get_environment(float *temperature, float *humidity)
{
  float local_temp = 0.0f;
  float local_humi = MQ135_TEMP_CORR_REF_HUMIDITY;
  bool valid = false;

  if (dht22_getcurrenttemperature(&local_temp) == SYSTEM_ERROR_NONE)
  {
    *temperature = local_temp;
    valid = true;
  }
  if (dht22_getcurrenthumidity(&local_humi) == SYSTEM_ERROR_NONE)
  {
    *humidity = local_humi;
  }
  else
  {
    *humidity = MQ135_TEMP_CORR_REF_HUMIDITY;
  }
  return valid;
}

// --- Public API ---

esp_err_t mq135_adc_init(void)
{
  if (s_adc_handle != NULL) return ESP_OK;

  ESP_RETURN_ON_ERROR(
      adc_helper_config_channel(MQ135_ADC_UNIT, MQ135_ADC_CHANNEL,
                                MQ135_ADC_BITWIDTH, MQ135_ADC_ATTEN),
      TAG_MQ135, "config channel failed");
  ESP_RETURN_ON_ERROR(adc_helper_get_handle(MQ135_ADC_UNIT, &s_adc_handle),
                      TAG_MQ135, "get handle failed");

  // Calibration
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cali_cfg = {
      .unit_id = MQ135_ADC_UNIT,
      .atten = MQ135_ADC_ATTEN,
      .bitwidth = MQ135_ADC_BITWIDTH,
  };
  esp_err_t ret =
      adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  adc_cali_line_fitting_config_t cali_cfg = {
      .unit_id = MQ135_ADC_UNIT,
      .atten = MQ135_ADC_ATTEN,
      .bitwidth = MQ135_ADC_BITWIDTH,
  };
  esp_err_t ret =
      adc_cali_create_scheme_line_fitting(&cali_cfg, &s_adc_cali_handle);
#else
  esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
#endif

  s_adc_calibrated = (ret == ESP_OK);
  if (!s_adc_calibrated)
  {
    ESP_LOGW(TAG_MQ135, "ADC calibration initialization failed");
  }
  return ESP_OK;
}

esp_err_t mq135_read_voltage(uint32_t *voltage_mv)
{
  if (s_adc_handle == NULL) return ESP_ERR_INVALID_STATE;

  int adc_raw;
  ESP_RETURN_ON_ERROR(
      adc_oneshot_read(s_adc_handle, MQ135_ADC_CHANNEL, &adc_raw), TAG_MQ135,
      "ADC read failed");

  if (s_adc_calibrated)
  {
    int cal_voltage = 0;
    ESP_RETURN_ON_ERROR(
        adc_cali_raw_to_voltage(s_adc_cali_handle, adc_raw, &cal_voltage),
        TAG_MQ135, "Calibrated convert failed");
    *voltage_mv = (uint32_t)cal_voltage;
  }
  else
  {
    *voltage_mv = (uint32_t)(((uint64_t)adc_raw * MQ135_ADC_FULL_SCALE_MV) /
                             MQ135_ADC_MAX_READING);
  }
  return ESP_OK;
}

esp_err_t mq135_read_aqi(int *aqi)
{
  if (s_adc_handle == NULL) return ESP_ERR_INVALID_STATE;

  uint32_t voltage = 0;
  esp_err_t ret = mq135_read_voltage(&voltage);
  if (ret != ESP_OK) return ret;

  // Environmental Compensation
  float temp = 25.0f;
  float humi = 50.0f;
  bool has_env = mq135_get_environment(&temp, &humi);
  float correction_factor =
      has_env ? mq135_compute_temperature_correction(temp, humi) : 1.0f;

  float compensated_voltage = (float)voltage;
  if (correction_factor > 0.0f)
  {
    compensated_voltage /= correction_factor;
  }

  // Protection
  if (compensated_voltage < 0.0f) compensated_voltage = 0.0f;
  if (compensated_voltage > 5000.0f) compensated_voltage = 5000.0f;

  int comp_mv = (int)(compensated_voltage + 0.5f);

  syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_DEBUG,
                 "MQ135 Raw: %d mV, Comp: %d mV (T:%.1f H:%.1f)", voltage,
                 comp_mv, temp, humi);

  // Calculate AQI (Legacy Logic)
  if (comp_mv <= MQ135_SW_BASE_VOLTAGE)
  {
    *aqi = 0;
  }
  else
  {
    *aqi = ((comp_mv - MQ135_SW_BASE_VOLTAGE) / 100) * 20;
  }

  return ESP_OK;
}
