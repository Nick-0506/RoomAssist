#include "adc_helper.h"

#include "esp_check.h"

static const char *TAG = "adc_helper";
#define ADC_HELPER_UNIT_COUNT  (ADC_UNIT_2 + 1)
static adc_oneshot_unit_handle_t s_unit_handles[ADC_HELPER_UNIT_COUNT] = {0};

esp_err_t adc_helper_get_handle(adc_unit_t unit, adc_oneshot_unit_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "out_handle is NULL");
    ESP_RETURN_ON_FALSE(unit < ADC_HELPER_UNIT_COUNT, ESP_ERR_INVALID_ARG, TAG, "invalid unit");

    if (s_unit_handles[unit] == NULL) {
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = unit,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_cfg, &s_unit_handles[unit]), TAG, "create oneshot unit failed");
    }

    *out_handle = s_unit_handles[unit];
    return ESP_OK;
}

esp_err_t adc_helper_config_channel(adc_unit_t unit, adc_channel_t channel, adc_bitwidth_t bitwidth, adc_atten_t atten)
{
    adc_oneshot_unit_handle_t handle = NULL;
    ESP_RETURN_ON_ERROR(adc_helper_get_handle(unit, &handle), TAG, "get handle failed");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = bitwidth,
        .atten = atten,
    };

    esp_err_t ret = adc_oneshot_config_channel(handle, channel, &chan_cfg);
    if (ret == ESP_ERR_INVALID_STATE) {
        // Channel already configured by other module, treat as success
        return ESP_OK;
    }
    return ret;
}
