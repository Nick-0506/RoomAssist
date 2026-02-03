/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "ir_zro_encoder.h"
#include "syslog.h"
#include "system.h"

static const char *TAG = "zro_encoder";
SemaphoreHandle_t gsemaIRZEROCfg = NULL;     //Created in rmt.c

bool gzerofanstatus = 0;
int gzerofanspeed = 0;
int gzerofanswing = 0;

typedef struct {
    rmt_encoder_t base;           // the base "class", declares the standard encoder interface
    rmt_encoder_t *copy_encoder;  // use the copy_encoder to encode the leading and ending pulse
    rmt_encoder_t *bytes_encoder; // use the bytes_encoder to encode the address and command data
    rmt_symbol_word_t zro_leading_symbol; // +-0 leading code with RMT representation
    rmt_symbol_word_t zro_repeat_symbol;  // +-0 leading code with RMT representation
    rmt_symbol_word_t zro_ending_symbol;  // +-0 ending code with RMT representation
    int state;
} rmt_ir_zro_encoder_t;

bool rmt_iszerofanactive()
{
    bool ret = false;
    if(gsemaIRZEROCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (zro %d)",__LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaIRZEROCfg, portMAX_DELAY) == pdTRUE) 
    {
        ret = gzerofanstatus;
        xSemaphoreGive(gsemaIRZEROCfg);
    }
    return ret;
}

int rmt_setzerofanstatus(bool status)
{
    if(gsemaIRZEROCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (zro %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRZEROCfg, portMAX_DELAY) == pdTRUE) 
    {
        gzerofanstatus = status;
        xSemaphoreGive(gsemaIRZEROCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_getzerofanspeed(int *value)
{
    if(gsemaIRZEROCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (zro %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRZEROCfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gzerofanspeed;
        xSemaphoreGive(gsemaIRZEROCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setzerofanspeed(int value)
{
    if(gsemaIRZEROCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (zro %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRZEROCfg, portMAX_DELAY) == pdTRUE) 
    {
        gzerofanspeed = value;
        xSemaphoreGive(gsemaIRZEROCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_getzerofanswing(int *value)
{
    if(gsemaIRZEROCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready");        
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRZEROCfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gzerofanswing;
        xSemaphoreGive(gsemaIRZEROCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setzerofanswing(int value)
{
    if(gsemaIRZEROCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (zro %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRZEROCfg, portMAX_DELAY) == pdTRUE) 
    {
        gzerofanswing = value;
        xSemaphoreGive(gsemaIRZEROCfg);
    }
    return SYSTEM_ERROR_NONE;
}

void ir_zerofan_restoreconfig(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    uint32_t value1 = 0;
    gsemaIRZEROCfg = xSemaphoreCreateBinary();
    if (gsemaIRZEROCfg == NULL) {
        return;        
    }

    ret = nvs_open(ZEROFAN_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(gsemaIRZEROCfg);
        return;
    }

    ret = nvs_get_u32(nvs_handle, ZEROFAN_NVS_STATUS_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gzerofanstatus = (bool)value1;
    }
    else
    {
        gzerofanstatus = (bool)ZERO_FAN_DEFAULT_STATUS;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

        ret = nvs_get_u32(nvs_handle, ZEROFAN_NVS_SPEED_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gzerofanspeed = value1;
    }
    else
    {
        gzerofanspeed = ZERO_FAN_DEFAULT_SPEED;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    ret = nvs_get_u32(nvs_handle, ZEROFAN_NVS_SWING_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gzerofanswing = value1;
    }
    else
    {
        gzerofanswing = ZERO_FAN_DEFAULT_SWING;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    xSemaphoreGive(gsemaIRZEROCfg);
    return;
}

static size_t rmt_encode_ir_zro(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_ir_zro_encoder_t *zro_encoder = __containerof(encoder, rmt_ir_zro_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    ir_zro_scan_code_t *scan_code = (ir_zro_scan_code_t *)primary_data;
    rmt_encoder_handle_t copy_encoder = zro_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = zro_encoder->bytes_encoder;

    switch (zro_encoder->state) {
        case 0: // send leading code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &zro_encoder->zro_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                zro_encoder->state = 1; // we can only switch to next state when current encoder finished
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {             
                state |= RMT_ENCODING_MEM_FULL;
                goto out; // yield if there's no free space to put other encoding artifacts
            }
            // fall-through
        case 1: // send address
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->data, data_size/*sizeof(uint8_t)*/, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) 
            {
                //zro_encoder->state = 2; // we can only switch to next state when current encoder finished
            }
            for(int i=0;i<=0;i++)
            {
                encoded_symbols += copy_encoder->encode(copy_encoder, channel, &zro_encoder->zro_repeat_symbol,
                    sizeof(rmt_symbol_word_t), &session_state);

                encoded_symbols += copy_encoder->encode(copy_encoder, channel, &zro_encoder->zro_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);

                encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->data, data_size/*sizeof(uint8_t)*/, &session_state);
            
                if (session_state & RMT_ENCODING_MEM_FULL) 
                {
                    state |= RMT_ENCODING_MEM_FULL;
                    goto out; // yield if there's no free space to put other encoding artifacts
                }
            }
            if (session_state & RMT_ENCODING_COMPLETE) 
            {
                zro_encoder->state = 2; // we can only switch to next state when current encoder finished
            }
            // fall-through
        case 2: // send ending code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &zro_encoder->zro_ending_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                zro_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
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

static esp_err_t rmt_del_ir_zro_encoder(rmt_encoder_t *encoder)
{
    rmt_ir_zro_encoder_t *zro_encoder = __containerof(encoder, rmt_ir_zro_encoder_t, base);
    rmt_del_encoder(zro_encoder->copy_encoder);
    rmt_del_encoder(zro_encoder->bytes_encoder);
    free(zro_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_zro_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_ir_zro_encoder_t *zro_encoder = __containerof(encoder, rmt_ir_zro_encoder_t, base);
    rmt_encoder_reset(zro_encoder->copy_encoder);
    rmt_encoder_reset(zro_encoder->bytes_encoder);
    zro_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t rmt_new_ir_zro_encoder(const ir_zro_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_ir_zro_encoder_t *zro_encoder = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    zro_encoder = calloc(1, sizeof(rmt_ir_zro_encoder_t));
    ESP_GOTO_ON_FALSE(zro_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for ir zro encoder");
    zro_encoder->base.encode = rmt_encode_ir_zro;
    zro_encoder->base.del = rmt_del_ir_zro_encoder;
    zro_encoder->base.reset = rmt_ir_zro_encoder_reset;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &zro_encoder->copy_encoder), err, TAG, "create copy encoder failed");

    // construct the leading code and ending code with RMT symbol format
    zro_encoder->zro_leading_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 2500ULL * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 3300ULL * config->resolution / 1000000,
    };
    zro_encoder->zro_repeat_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 450ULL * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 6600ULL * config->resolution / 1000000,
    };
    zro_encoder->zro_ending_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 450 * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 0,
    };

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 450 * config->resolution / 1000000, // T0H=420us
            .level1 = 0,
            .duration1 = 1200 * config->resolution / 1000000, // T0L=420us
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 1300 * config->resolution / 1000000,  // T1H=420us
            .level1 = 0,
            .duration1 = 400 * config->resolution / 1000000, // T1L=1300us
        },
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &zro_encoder->bytes_encoder), err, TAG, "create bytes encoder failed");

    *ret_encoder = &zro_encoder->base;
    return ESP_OK;
err:
    if (zro_encoder) {
        if (zro_encoder->bytes_encoder) {
            rmt_del_encoder(zro_encoder->bytes_encoder);
        }
        if (zro_encoder->copy_encoder) {
            rmt_del_encoder(zro_encoder->copy_encoder);
        }
        free(zro_encoder);
    }
    return ret;
}
