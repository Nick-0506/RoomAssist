/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "ir_hta_encoder.h"
#include "syslog.h"
#include "system.h"
#include "rmt.h"

int gac_status = AC_DEFAULT_STATUS;
int gac_type = AC_DEFAULT_TYPE;
int gac_temp = AC_DEFAULT_TEMP;
int gac_speed = AC_DEFAULT_SPEED;
int gac_swing = AC_DEFAULT_SWING;

SemaphoreHandle_t gsemaIRACCfg = NULL;      //Created in rmt.c

static const char *TAG = "hta_encoder";
typedef struct {
    rmt_encoder_t base;           // the base "class", declares the standard encoder interface
    rmt_encoder_t *copy_encoder;  // use the copy_encoder to encode the leading and ending pulse
    rmt_encoder_t *bytes_encoder; // use the bytes_encoder to encode the address and command data
    rmt_symbol_word_t hta_leading_symbol; // HITACHI leading code with RMT representation
    rmt_symbol_word_t hta_ending_symbol;  // HITACHI ending code with RMT representation
    int state;
} rmt_ir_hta_encoder_t;

bool rmt_isiracactive()
{
    bool ret = false;
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        ret = gac_status;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return ret;
}

int rmt_setiracstatus(bool status)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        gac_status = status;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_getiracmode(int *value)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gac_type;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setiracmode(int value)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        gac_type = value;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_getiractemp(int *value)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gac_temp;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setiractemp(int value)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        gac_temp = value;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_getiracspeed(int *value)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gac_speed;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setiracspeed(int value)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        gac_speed = value;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_getiracswing(int *value)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gac_swing;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setiracswing(int value)
{
    if(gsemaIRACCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (hta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRACCfg, portMAX_DELAY) == pdTRUE) 
    {
        gac_swing = value;
        xSemaphoreGive(gsemaIRACCfg);
    }
    return SYSTEM_ERROR_NONE;
}

static size_t rmt_encode_ir_hta(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_ir_hta_encoder_t *hta_encoder = __containerof(encoder, rmt_ir_hta_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    ir_hta_scan_code_t *scan_code = (ir_hta_scan_code_t *)primary_data;
    rmt_encoder_handle_t copy_encoder = hta_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = hta_encoder->bytes_encoder;

    switch (hta_encoder->state) {
    case 0: // send leading code
        //printf("\n hta send leading\n\n");
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &hta_encoder->hta_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
                                         
        if (session_state & RMT_ENCODING_COMPLETE) {
            hta_encoder->state = 1; // we can only switch to next state when current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {           
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    // fall-through
    case 1: // send address
        //printf("\n hta send address\n\n");
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->data, data_size/*sizeof(uint8_t)*/, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            hta_encoder->state = 2; // we can only switch to next state when current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;           
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    // fall-through
    case 2: // send ending code
        //printf("\n hta send ending\n\n");
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &hta_encoder->hta_ending_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            hta_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_ir_hta_encoder(rmt_encoder_t *encoder)
{
    rmt_ir_hta_encoder_t *hta_encoder = __containerof(encoder, rmt_ir_hta_encoder_t, base);
    rmt_del_encoder(hta_encoder->copy_encoder);
    rmt_del_encoder(hta_encoder->bytes_encoder);
    free(hta_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_hta_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_ir_hta_encoder_t *hta_encoder = __containerof(encoder, rmt_ir_hta_encoder_t, base);
    rmt_encoder_reset(hta_encoder->copy_encoder);
    rmt_encoder_reset(hta_encoder->bytes_encoder);
    hta_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

void ir_ac_restoreconfig(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    uint32_t value1 = 0;

    gsemaIRACCfg = xSemaphoreCreateBinary();
    if (gsemaIRACCfg == NULL) {
        return;
    }
    
    ret = nvs_open(AC_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(gsemaIRACCfg);
        return;
    }

    ret = nvs_get_u32(nvs_handle, AC_NVS_STATUS_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gac_status = value1;
    }
    else
    {
        gac_status = AC_DEFAULT_STATUS;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    ret = nvs_get_u32(nvs_handle, AC_NVS_TYPE_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gac_type = value1;
    }
    else
    {
        gac_type = AC_DEFAULT_STATUS;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    ret = nvs_get_u32(nvs_handle, AC_NVS_TEMP_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gac_temp = value1;
    }
    else
    {
        gac_temp = AC_DEFAULT_TEMP;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    ret = nvs_get_u32(nvs_handle, AC_NVS_SPEED_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gac_speed = value1;
    }
    else
    {
        gac_speed = AC_DEFAULT_SPEED;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    ret = nvs_get_u32(nvs_handle, AC_NVS_SWING_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gac_swing = value1;
    }
    else
    {
        gac_swing = AC_DEFAULT_SWING;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    xSemaphoreGive(gsemaIRACCfg);
    return;
}

esp_err_t rmt_new_ir_hta_encoder(const ir_hta_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_ir_hta_encoder_t *hta_encoder = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    hta_encoder = calloc(1, sizeof(rmt_ir_hta_encoder_t));
    ESP_GOTO_ON_FALSE(hta_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for ir hta encoder");
    hta_encoder->base.encode = rmt_encode_ir_hta;
    hta_encoder->base.del = rmt_del_ir_hta_encoder;
    hta_encoder->base.reset = rmt_ir_hta_encoder_reset;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &hta_encoder->copy_encoder), err, TAG, "create copy encoder failed");

    // construct the leading code and ending code with RMT symbol format
    hta_encoder->hta_leading_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 3400ULL * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 1600ULL * config->resolution / 1000000,
    };
    hta_encoder->hta_ending_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 420 * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 0,
    };

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 420 * config->resolution / 1000000, // T0H=420us
            .level1 = 0,
            .duration1 = 420 * config->resolution / 1000000, // T0L=420us
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 420 * config->resolution / 1000000,  // T1H=420us
            .level1 = 0,
            .duration1 = 1300 * config->resolution / 1000000, // T1L=1300us
        },
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &hta_encoder->bytes_encoder), err, TAG, "create bytes encoder failed");

    *ret_encoder = &hta_encoder->base;
    return ESP_OK;
err:
    if (hta_encoder) {
        if (hta_encoder->bytes_encoder) {
            rmt_del_encoder(hta_encoder->bytes_encoder);
        }
        if (hta_encoder->copy_encoder) {
            rmt_del_encoder(hta_encoder->copy_encoder);
        }
        free(hta_encoder);
    }
    return ret;
}
