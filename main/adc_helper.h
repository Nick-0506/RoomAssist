#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t adc_helper_get_handle(adc_unit_t unit, adc_oneshot_unit_handle_t *out_handle);
esp_err_t adc_helper_config_channel(adc_unit_t unit, adc_channel_t channel, adc_bitwidth_t bitwidth, adc_atten_t atten);

#ifdef __cplusplus
}
#endif
