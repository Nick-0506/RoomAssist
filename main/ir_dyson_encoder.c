/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "ir_dyson_encoder.h"

static const char *TAG = "dyson_encoder";

typedef struct {
    rmt_encoder_t base;           // the base "class", declares the standard encoder interface
    rmt_encoder_t *copy_encoder;  // use the copy_encoder to encode the leading and ending pulse
    rmt_encoder_t *bytes_encoder; // use the bytes_encoder to encode the address and command data
    rmt_symbol_word_t dyson_leading_symbol; // +-0 leading code with RMT representation
    rmt_symbol_word_t dyson_repeat_symbol;  // +-0 leading code with RMT representation
    rmt_symbol_word_t dyson_ending_symbol;  // +-0 ending code with RMT representation
    int state;
} rmt_ir_dyson_encoder_t;

static size_t rmt_encode_ir_dyson(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_ir_dyson_encoder_t *dyson_encoder = __containerof(encoder, rmt_ir_dyson_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    ir_dyson_scan_code_t *scan_code = (ir_dyson_scan_code_t *)primary_data;
    rmt_encoder_handle_t copy_encoder = dyson_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = dyson_encoder->bytes_encoder;

    switch (dyson_encoder->state) {
        case 0: // send leading code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &dyson_encoder->dyson_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                dyson_encoder->state = 1; // we can only switch to next state when current encoder finished
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
                //dyson_encoder->state = 2; // we can only switch to next state when current encoder finished
            }
            for(int i=0;i<=0;i++)
            {
                encoded_symbols += copy_encoder->encode(copy_encoder, channel, &dyson_encoder->dyson_repeat_symbol,
                    sizeof(rmt_symbol_word_t), &session_state);

                encoded_symbols += copy_encoder->encode(copy_encoder, channel, &dyson_encoder->dyson_leading_symbol,
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
                dyson_encoder->state = 2; // we can only switch to next state when current encoder finished
            }
            // fall-through
        case 2: // send ending code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &dyson_encoder->dyson_ending_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                dyson_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
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

static esp_err_t rmt_del_ir_dyson_encoder(rmt_encoder_t *encoder)
{
    rmt_ir_dyson_encoder_t *dyson_encoder = __containerof(encoder, rmt_ir_dyson_encoder_t, base);
    rmt_del_encoder(dyson_encoder->copy_encoder);
    rmt_del_encoder(dyson_encoder->bytes_encoder);
    free(dyson_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_dyson_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_ir_dyson_encoder_t *dyson_encoder = __containerof(encoder, rmt_ir_dyson_encoder_t, base);
    rmt_encoder_reset(dyson_encoder->copy_encoder);
    rmt_encoder_reset(dyson_encoder->bytes_encoder);
    dyson_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t rmt_new_ir_dyson_encoder(const ir_dyson_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_ir_dyson_encoder_t *dyson_encoder = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    dyson_encoder = calloc(1, sizeof(rmt_ir_dyson_encoder_t));
    ESP_GOTO_ON_FALSE(dyson_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for ir dyson encoder");
    dyson_encoder->base.encode = rmt_encode_ir_dyson;
    dyson_encoder->base.del = rmt_del_ir_dyson_encoder;
    dyson_encoder->base.reset = rmt_ir_dyson_encoder_reset;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &dyson_encoder->copy_encoder), err, TAG, "create copy encoder failed");

    // construct the leading code and ending code with RMT symbol format
    dyson_encoder->dyson_leading_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 2500ULL * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 3300ULL * config->resolution / 1000000,
    };
    dyson_encoder->dyson_repeat_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 450ULL * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 6600ULL * config->resolution / 1000000,
    };
    dyson_encoder->dyson_ending_symbol = (rmt_symbol_word_t) {
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
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &dyson_encoder->bytes_encoder), err, TAG, "create bytes encoder failed");

    *ret_encoder = &dyson_encoder->base;
    return ESP_OK;
err:
    if (dyson_encoder) {
        if (dyson_encoder->bytes_encoder) {
            rmt_del_encoder(dyson_encoder->bytes_encoder);
        }
        if (dyson_encoder->copy_encoder) {
            rmt_del_encoder(dyson_encoder->copy_encoder);
        }
        free(dyson_encoder);
    }
    return ret;
}
