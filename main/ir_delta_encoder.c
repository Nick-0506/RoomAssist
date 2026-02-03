/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "ir_delta_encoder.h"
#include "rmt.h"
#include "system.h"
#include "syslog.h"

static const char *TAG = "delta_encoder";
SemaphoreHandle_t gsemaIRDELTACfg = NULL;       //Created in rmt.c

bool gmanualfanstatus = false;  // delta fan manual
bool gdryfanstatus = false;     // delta fan dry
bool gwarmfanstatus = false;    // delta fan warm
bool gexhaustfanstatus = false; // delta fan exhaust

typedef struct {
    rmt_encoder_t base;           // the base "class", declares the standard encoder interface
    rmt_encoder_t *copy_encoder;  // use the copy_encoder to encode the leading and ending pulse
    rmt_encoder_t *bytes_encoder; // use the bytes_encoder to encode the address and command data
    rmt_symbol_word_t delta_leading_symbol; // +-0 leading code with RMT representation
    rmt_symbol_word_t delta_middle_symbol;  // +-0 leading code with RMT representation
    rmt_symbol_word_t delta_ending_symbol;  // +-0 ending code with RMT representation
    int state;
} rmt_ir_delta_encoder_t;


bool rmt_ismanualfanactive()
{
    bool ret = false;
    if(gsemaIRDELTACfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (delta %d)",__LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaIRDELTACfg, portMAX_DELAY) == pdTRUE) 
    {
        ret = gmanualfanstatus;
        xSemaphoreGive(gsemaIRDELTACfg);
    }
    return ret;
}

bool rmt_isdryfanactive()
{
    bool ret = false;
    if(gsemaIRDELTACfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (delta %d)",__LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaIRDELTACfg, portMAX_DELAY) == pdTRUE) 
    {
        ret = gdryfanstatus;
        xSemaphoreGive(gsemaIRDELTACfg);
    }
    return ret;
}

bool rmt_iswarmfanactive()
{
    bool ret = false;
    if(gsemaIRDELTACfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (delta %d)",__LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaIRDELTACfg, portMAX_DELAY) == pdTRUE) 
    {
        ret = gwarmfanstatus;
        xSemaphoreGive(gsemaIRDELTACfg);
    }
    return ret;
}

bool rmt_isexhaustfanactive()
{
    bool ret = false;
    if(gsemaIRDELTACfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (delta %d)",__LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaIRDELTACfg, portMAX_DELAY) == pdTRUE) 
    {
        ret = gexhaustfanstatus;
        xSemaphoreGive(gsemaIRDELTACfg);
    }
    return ret;
}

int rmt_setmanualfanstatus(bool status)
{
    if(gsemaIRDELTACfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (delta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRDELTACfg, portMAX_DELAY) == pdTRUE) 
    {
        gmanualfanstatus = status;
        xSemaphoreGive(gsemaIRDELTACfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setdryfanstatus(bool status)
{
    if(gsemaIRDELTACfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (delta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRDELTACfg, portMAX_DELAY) == pdTRUE) 
    {
        gdryfanstatus = status;
        xSemaphoreGive(gsemaIRDELTACfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setwarmfanstatus(bool status)
{
    if(gsemaIRDELTACfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (delta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRDELTACfg, portMAX_DELAY) == pdTRUE) 
    {
        gwarmfanstatus = status;
        xSemaphoreGive(gsemaIRDELTACfg);
    }
    return SYSTEM_ERROR_NONE;
}

int rmt_setexhaustfanstatus(bool status)
{
    if(gsemaIRDELTACfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_IR,SYSLOG_LEVEL_ERROR,"Semaphore not ready (delta %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaIRDELTACfg, portMAX_DELAY) == pdTRUE) 
    {
        gexhaustfanstatus = status;
        xSemaphoreGive(gsemaIRDELTACfg);
    }
    return SYSTEM_ERROR_NONE;
}

static size_t rmt_encode_ir_delta(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_ir_delta_encoder_t *delta_encoder = __containerof(encoder, rmt_ir_delta_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    ir_delta_scan_code_t *scan_code = (ir_delta_scan_code_t *)primary_data;
    rmt_encoder_handle_t copy_encoder = delta_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = delta_encoder->bytes_encoder;

    switch (delta_encoder->state) {
        case 0: // send leading code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &delta_encoder->delta_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                delta_encoder->state = 1; // we can only switch to next state when current encoder finished
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {             
                state |= RMT_ENCODING_MEM_FULL;
                goto out; // yield if there's no free space to put other encoding artifacts
            }
            // fall-through
        case 1: // send address
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->data, 4, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                delta_encoder->state = 2; // we can only switch to next state when current encoder finished
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;           
                goto out; // yield if there's no free space to put other encoding artifacts
            }
        case 2: // send middle code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &delta_encoder->delta_middle_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &delta_encoder->delta_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                delta_encoder->state = 3; // we can only switch to next state when current encoder finished
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {             
                state |= RMT_ENCODING_MEM_FULL;
                goto out; // yield if there's no free space to put other encoding artifacts
            }
        case 3: // send address
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->time, 4, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                delta_encoder->state = 4; // we can only switch to next state when current encoder finished
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;           
                goto out; // yield if there's no free space to put other encoding artifacts
            }
        case 4: // send ending code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &delta_encoder->delta_ending_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                delta_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
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

static esp_err_t rmt_del_ir_delta_encoder(rmt_encoder_t *encoder)
{
    rmt_ir_delta_encoder_t *delta_encoder = __containerof(encoder, rmt_ir_delta_encoder_t, base);
    rmt_del_encoder(delta_encoder->copy_encoder);
    rmt_del_encoder(delta_encoder->bytes_encoder);
    free(delta_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_delta_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_ir_delta_encoder_t *delta_encoder = __containerof(encoder, rmt_ir_delta_encoder_t, base);
    rmt_encoder_reset(delta_encoder->copy_encoder);
    rmt_encoder_reset(delta_encoder->bytes_encoder);
    delta_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

void ir_deltafan_restoreconfig(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    uint32_t value1 = 0;
    
    gsemaIRDELTACfg = xSemaphoreCreateBinary();
    if (gsemaIRDELTACfg == NULL) {
        return;
    }

    ret = nvs_open(DELTAFAN_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(gsemaIRDELTACfg);
        return;
    }

    ret = nvs_get_u32(nvs_handle, DELTAFAN_NVS_STATUS_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gmanualfanstatus = (bool)value1;
    }
    else
    {
        gmanualfanstatus = (bool)DELTA_FAN_DEFAULT_STATUS;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }
    nvs_close(nvs_handle);
    xSemaphoreGive(gsemaIRDELTACfg);
    return;
}

esp_err_t rmt_new_ir_delta_encoder(const ir_delta_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_ir_delta_encoder_t *delta_encoder = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    delta_encoder = calloc(1, sizeof(rmt_ir_delta_encoder_t));
    ESP_GOTO_ON_FALSE(delta_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for ir delta encoder");
    delta_encoder->base.encode = rmt_encode_ir_delta;
    delta_encoder->base.del = rmt_del_ir_delta_encoder;
    delta_encoder->base.reset = rmt_ir_delta_encoder_reset;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &delta_encoder->copy_encoder), err, TAG, "create copy encoder failed");

    // construct the leading code and ending code with RMT symbol format
    delta_encoder->delta_leading_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 8950ULL * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 4500ULL * config->resolution / 1000000,
    };

    delta_encoder->delta_middle_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 590ULL * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 19910ULL * config->resolution / 1000000,
    };

    delta_encoder->delta_ending_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 590ULL * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 0,
    };

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 540ULL * config->resolution / 1000000, // T0H=420us
            .level1 = 0,
            .duration1 = 600ULL * config->resolution / 1000000, // T0L=420us
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 540ULL * config->resolution / 1000000,  // T1H=420us
            .level1 = 0,
            .duration1 = 1690ULL * config->resolution / 1000000, // T1L=1300us
        },
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &delta_encoder->bytes_encoder), err, TAG, "create bytes encoder failed");

    *ret_encoder = &delta_encoder->base;
    return ESP_OK;
err:
    if (delta_encoder) {
        if (delta_encoder->bytes_encoder) {
            rmt_del_encoder(delta_encoder->bytes_encoder);
        }
        if (delta_encoder->copy_encoder) {
            rmt_del_encoder(delta_encoder->copy_encoder);
        }
        free(delta_encoder);
    }
    return ret;
}
