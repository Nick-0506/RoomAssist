#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "nu_ld2410.h"
#include "ld2410.h"
#include "syslog.h"
#include "system.h"
#include <nvs_flash.h>
#include "esp_timer.h"

/* Base64 encoding/decoding - self-implemented to avoid mbedtls dependency */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_encode(char *dst, size_t dlen, size_t *olen,
                         const unsigned char *src, size_t slen)
{
    size_t i, n;
    int C1, C2, C3;
    char *p;

    if (slen == 0)
    {
        *olen = 0;
        return 0;
    }

    n = (slen + 2) / 3 * 4;
    if (dlen < n + 1)
    {
        *olen = n + 1;
        return -1; /* Buffer too small */
    }

    p = dst;
    for (i = 0; i < slen; i += 3)
    {
        C1 = src[i];
        C2 = (i + 1 < slen) ? src[i + 1] : 0;
        C3 = (i + 2 < slen) ? src[i + 2] : 0;

        *p++ = b64_table[(C1 >> 2) & 0x3F];
        *p++ = b64_table[((C1 & 0x03) << 4) | ((C2 >> 4) & 0x0F)];
        *p++ = (i + 1 < slen)
                   ? b64_table[((C2 & 0x0F) << 2) | ((C3 >> 6) & 0x03)]
                   : '=';
        *p++ = (i + 2 < slen) ? b64_table[C3 & 0x3F] : '=';
    }
    *p = '\0';
    *olen = p - dst;
    return 0;
}

static int base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                         const char *src, size_t slen)
{
    size_t i, n;
    unsigned char *p;
    unsigned char dec_map[128];

    /* Build decoding map */
    memset(dec_map, 0x80, sizeof(dec_map));
    for (i = 0; i < 64; i++)
    {
        dec_map[(unsigned char)b64_table[i]] = i;
    }
    dec_map['='] = 0;

    /* Calculate output length */
    n = (slen / 4) * 3;
    if (slen > 0 && src[slen - 1] == '=') n--;
    if (slen > 1 && src[slen - 2] == '=') n--;

    if (dlen < n)
    {
        *olen = n;
        return -1; /* Buffer too small */
    }

    p = dst;
    for (i = 0; i < slen; i += 4)
    {
        unsigned char a = dec_map[(unsigned char)src[i]];
        unsigned char b =
            (i + 1 < slen) ? dec_map[(unsigned char)src[i + 1]] : 0;
        unsigned char c =
            (i + 2 < slen) ? dec_map[(unsigned char)src[i + 2]] : 0;
        unsigned char d =
            (i + 3 < slen) ? dec_map[(unsigned char)src[i + 3]] : 0;

        if ((a | b | c | d) & 0x80)
        {
            return -2; /* Invalid character */
        }

        *p++ = (a << 2) | (b >> 4);
        if (i + 2 < slen && src[i + 2] != '=')
        {
            *p++ = (b << 4) | (c >> 2);
        }
        if (i + 3 < slen && src[i + 3] != '=')
        {
            *p++ = (c << 6) | d;
        }
    }
    *olen = p - dst;
    return 0;
}

float input[INPUT_SIZE];
float hidden[HIDDEN_SIZE];
float hidden_raw[HIDDEN_SIZE];
float output[OUTPUT_SIZE];

float w_input_hidden[INPUT_SIZE][HIDDEN_SIZE];
float w_hidden_output[HIDDEN_SIZE][OUTPUT_SIZE];

float hidden_bias[HIDDEN_SIZE];
float output_bias[OUTPUT_SIZE];
float saved_output_bias[OUTPUT_SIZE];

float gnuld2410_pred = 0;

/* Auto complete learning */
float gnuld2410_someone_threshold = 0.6;
int gnuld2410_someone_threshold_counter = 0;
float gnuld2410_noone_threshold = 0.3;
int gnuld2410_noone_threshold_counter = 0;

float sensor_buffer[TIME_WINDOW][SENSOR_SIZE];
int sensor_index = 0;
int sample_count = 0;

float history_inputs[HISTORY_SIZE][INPUT_SIZE];
int history_index = 0;
int history_someonemv_count = 0;
int history_someone_count = 0;
int history_noone_count = 0;

int64_t gnuld2410_less_someoneth_time = 0, gnuld2410_less_nooneth_time = 0;
uint32_t gnuld2410_keep_nooneth_counter = 0;

SemaphoreHandle_t gsemaNULD2410Cfg = NULL;

int nu_ld2410_getPred(float *pred)
{
    if (gsemaNULD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (nu %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaNULD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        *pred = gnuld2410_pred;
        xSemaphoreGive(gsemaNULD2410Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int nu_ld2410_setPred(float pred)
{
    if (gsemaNULD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (nu %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaNULD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        gnuld2410_pred = pred;
        xSemaphoreGive(gsemaNULD2410Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

void nu_ld2410_add_sensor_input_to_history(float *input_data, int target)
{
    /*
       Bigger index is recently
       noone:0~3, someone:4~7
    */
    int index = 0;
    int divd = (OUTPUT_SIZE == 1) ? 2 : (OUTPUT_SIZE);
    index = (target * ((HISTORY_SIZE) / divd) + history_index);

    if (index >= HISTORY_SIZE)
    {
        // dbg_printf("\n History_input index oversize.");
        return;
    }

    memcpy(history_inputs[index], input_data, sizeof(float) * INPUT_SIZE);

    /* history_index from 0 to 7 */
    history_index = (history_index + 1) % ((HISTORY_SIZE) / divd);

    if (history_noone_count < ((HISTORY_SIZE) / divd) && target == 0)
    {
        history_noone_count++;
    }
    if (history_someone_count < ((HISTORY_SIZE) / divd) && target == 1)
    {
        history_someone_count++;
    }
    if (OUTPUT_SIZE == 3)
    {
        if (history_someonemv_count < ((HISTORY_SIZE) / divd) && target == 2)
        {
            history_someonemv_count++;
        }
    }
}

void show_to_history(void)
{
    /* noone:0~3, someone:4~7 */
    int i = 0;
    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                   "NU_LD2410 History");
    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                   "---------------------");
    for (i = 0; i < (HISTORY_SIZE); i++)
    {
        syslog_handler(
            SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
            "[%2d]Cur %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f "
            "%03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f "
            "%03.2f/%03.2f %03.2f, %03.2f",
            i + 1, history_inputs[i][0], history_inputs[i][1],
            history_inputs[i][2], history_inputs[i][3], history_inputs[i][4],
            history_inputs[i][5], history_inputs[i][6], history_inputs[i][7],
            history_inputs[i][8], history_inputs[i][9], history_inputs[i][10],
            history_inputs[i][11], history_inputs[i][12], history_inputs[i][13],
            history_inputs[i][14], history_inputs[i][15], history_inputs[i][16],
            history_inputs[i][17], history_inputs[i][18],
            history_inputs[i][19]);
        syslog_handler(
            SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
            "[%2d]Prv %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f "
            "%03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f "
            "%03.2f/%03.2f %03.2f, %03.2f",
            i + 1, history_inputs[i][20], history_inputs[i][21],
            history_inputs[i][22], history_inputs[i][23], history_inputs[i][24],
            history_inputs[i][25], history_inputs[i][26], history_inputs[i][27],
            history_inputs[i][28], history_inputs[i][29], history_inputs[i][30],
            history_inputs[i][31], history_inputs[i][32], history_inputs[i][33],
            history_inputs[i][34], history_inputs[i][35], history_inputs[i][36],
            history_inputs[i][37], history_inputs[i][38],
            history_inputs[i][39]);
        syslog_handler(
            SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
            "[%2d]Dif %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f "
            "%03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f %03.2f/%03.2f",
            i + 1, history_inputs[i][40], history_inputs[i][41],
            history_inputs[i][42], history_inputs[i][43], history_inputs[i][44],
            history_inputs[i][45], history_inputs[i][46], history_inputs[i][47],
            history_inputs[i][48], history_inputs[i][49], history_inputs[i][50],
            history_inputs[i][51], history_inputs[i][52], history_inputs[i][53],
            history_inputs[i][54], history_inputs[i][55]);
    }
    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                   "---------------------");
}

void show_to_w_ih(void)
{
    int j = 0;
    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                   "NU_LD2410 History\n");
    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                   "---------------------\n");
    for (j = 0; j < (HIDDEN_SIZE); j++)
    {
        syslog_handler(
            SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
            "[%2d] %3.2f/%3.2f %3.2f/%3.2f %3.2f/%3.2f %3.2f/%3.2f %3.2f/%3.2f "
            "%3.2f/%3.2f %3.2f/%3.2f %3.2f/%3.2f %3.2f/%3.2f %3.2f, %3.2f",
            j + 1, w_input_hidden[0][j], w_input_hidden[1][j],
            w_input_hidden[2][j], w_input_hidden[3][j], w_input_hidden[4][j],
            w_input_hidden[5][j], w_input_hidden[6][j], w_input_hidden[7][j],
            w_input_hidden[8][j], w_input_hidden[9][j], w_input_hidden[10][j],
            w_input_hidden[11][j], w_input_hidden[12][j], w_input_hidden[13][j],
            w_input_hidden[14][j], w_input_hidden[15][j], w_input_hidden[16][j],
            w_input_hidden[17][j], w_input_hidden[18][j],
            w_input_hidden[19][j]);
    }
    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                   "---------------------\n");
}

bool nu_ld2410_isnew(void)
{
    for (int i = 0; i < OUTPUT_SIZE; i++)
    {
        if (saved_output_bias[i] != output_bias[i])
        {
            return true;
        }
    }
    return false;
}

bool nu_ld2410_saveweights(void)
{
    FILE *f;
    size_t written;

    f = fopen(W_IH_PATH, "wb");
    if (!f)
    {
        return false;
    }
    written =
        fwrite(w_input_hidden, sizeof(float), INPUT_SIZE * HIDDEN_SIZE, f);
    fclose(f);
    if (written != INPUT_SIZE * HIDDEN_SIZE)
    {
        return false;
    }

    f = fopen(W_HO_PATH, "wb");
    if (!f)
    {
        return false;
    }
    written =
        fwrite(w_hidden_output, sizeof(float), HIDDEN_SIZE * OUTPUT_SIZE, f);
    fclose(f);
    if (written != HIDDEN_SIZE * OUTPUT_SIZE)
    {
        return false;
    }

    f = fopen(B_H_PATH, "wb");
    if (!f)
    {
        return false;
    }
    written = fwrite(hidden_bias, sizeof(float), HIDDEN_SIZE, f);
    fclose(f);
    if (written != HIDDEN_SIZE)
    {
        return false;
    }

    f = fopen(B_O_PATH, "wb");
    if (!f)
    {
        return false;
    }
    written = fwrite(output_bias, sizeof(float), OUTPUT_SIZE, f);
    fclose(f);
    if (written != OUTPUT_SIZE)
    {
        return false;
    }

    for (int i = 0; i < OUTPUT_SIZE; i++)
    {
        saved_output_bias[i] = output_bias[i];
    }

    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_INFO, "Config saved");
    return true;
}

bool nu_ld2410_restoreweights(void)
{
    bool ret = true;
    size_t read;
    size_t size;
    FILE *f;

    gsemaNULD2410Cfg = xSemaphoreCreateBinary();
    if (gsemaNULD2410Cfg == NULL)
    {
        return false;
    }
    size = sizeof(w_input_hidden);
    f = fopen(W_IH_PATH, "rb");
    if (!f)
    {
        ret = false;
    }
    else
    {
        read =
            fread(w_input_hidden, sizeof(float), INPUT_SIZE * HIDDEN_SIZE, f);
        if (read != INPUT_SIZE * HIDDEN_SIZE)
        {
            ret = false;
        }
        fclose(f);
    }

    size = sizeof(w_hidden_output);
    f = fopen(W_HO_PATH, "rb");
    if (!f)
    {
        ret = false;
    }
    else
    {
        read =
            fread(w_hidden_output, sizeof(float), HIDDEN_SIZE * OUTPUT_SIZE, f);
        if (read != HIDDEN_SIZE * OUTPUT_SIZE)
        {
            ret = false;
        }
        fclose(f);
    }

    size = sizeof(hidden_bias);
    f = fopen(B_H_PATH, "rb");
    if (!f)
    {
        ret = false;
    }
    else
    {
        read = fread(hidden_bias, sizeof(float), HIDDEN_SIZE, f);
        if (read != HIDDEN_SIZE)
        {
            ret = false;
        }
        fclose(f);
    }

    size = sizeof(output_bias);
    f = fopen(B_O_PATH, "rb");
    if (!f)
    {
        ret = false;
    }
    else
    {
        read = fread(output_bias, sizeof(float), OUTPUT_SIZE, f);
        if (read != OUTPUT_SIZE)
        {
            ret = false;
        }
        fclose(f);
    }

    for (int i = 0; i < OUTPUT_SIZE; i++)
    {
        saved_output_bias[i] = output_bias[i];
    }

    xSemaphoreGive(gsemaNULD2410Cfg);
    return ret;
}

void nu_ld2410_resetbuffer(void)
{
    memset(input, 0, sizeof(input));
    memset(hidden, 0, sizeof(hidden));
    memset(output, 0, sizeof(output));
    memset(sensor_buffer, 0, sizeof(sensor_buffer));
    sensor_index = 0;
    sample_count = 0;
}

int argmax(float *arr, int size)
{
    int max_idx = 0;
    for (int i = 1; i < size; i++)
    {
        if (arr[i] > arr[max_idx])
        {
            max_idx = i;
        }
    }
    return max_idx;
}

float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

float d_sigmoid(float y) { return y * (1.0f - y); }

float relu(float x) { return x > 0 ? x : 0; }

float d_relu(float x) { return x > 0 ? 1.0f : 0.0f; }

float leaky_relu(float x) { return x > 0 ? x : 0.01f * x; }

float d_leaky_relu(float x) { return x > 0 ? 1.0f : 0.01f; }

void nu_ld2410_init_weights()
{
    float stddev_input_hidden = sqrtf(2.0f / INPUT_SIZE);
    float stddev_hidden_output = sqrtf(2.0f / HIDDEN_SIZE);

    for (int i = 0; i < INPUT_SIZE; i++)
    {
        for (int j = 0; j < HIDDEN_SIZE; j++)
        {
            w_input_hidden[i][j] =
                ((float)rand() / RAND_MAX * 2.0f - 1.0f) * stddev_input_hidden;
        }
    }

    for (int j = 0; j < HIDDEN_SIZE; j++)
    {
        hidden_bias[j] = 0.0f;  //  bias 初始化為 0
        for (int i = 0; i < OUTPUT_SIZE; i++)
        {
            w_hidden_output[j][i] =
                ((float)rand() / RAND_MAX * 2.0f - 1.0f) * stddev_hidden_output;
        }
    }
    for (int i = 0; i < OUTPUT_SIZE; i++)
    {
        output_bias[i] = 0.0f;  // 初始化為 0
    }
}

void nu_ld2410_build_input_from_buffer(bool istraining)
{
    float noise = 0, raw = 0, pre_raw = 0, normalized = 0;
    int current_index = sensor_index % TIME_WINDOW;
    int previous_index = (TIME_WINDOW + sensor_index - 1) % TIME_WINDOW;
    for (int i = 0; i < SENSOR_SIZE; i++)
    {
        raw = sensor_buffer[current_index][i];
        normalized = raw / 100.0f;
        input[i] = normalized + noise;
        if (TIME_WINDOW > 1)
        {
            pre_raw = sensor_buffer[previous_index][i];
            normalized = pre_raw / 100.0f;
            input[i + SENSOR_SIZE] = input[i] - normalized + noise;
        }
    }
    if (NEAR_DOOR > 0)
    {
        for (int i = 0; i < (NEAR_DOOR / 2) && i * 2 + 3 < INPUT_SIZE; i++)
        {
            input[SENSOR_SIZE * TIME_WINDOW + i * 2] =
                input[i * 2] - input[i * 2 + 2];
            input[SENSOR_SIZE * TIME_WINDOW + i * 2 + 1] =
                input[i * 2 + 1] - input[i * 2 + 3];
        }
    }
}

void nu_ld2410_build_input_from_history(int human_present)
{
    int idx = 0;
    int pick_index = 0;
    int divd = (OUTPUT_SIZE == 1) ? 2 : (OUTPUT_SIZE);

    pick_index = ((HISTORY_SIZE) / divd) * human_present +
                 random() % (((HISTORY_SIZE) / divd));

    for (int i = 0; i < INPUT_SIZE; i++)
    {
        input[idx++] = history_inputs[pick_index][i];
        ;
    }
}

float nu_ld2410_forward(float *input_data)
{
    for (int j = 0; j < HIDDEN_SIZE; j++)
    {
        float sum = hidden_bias[j];
        for (int i = 0; i < INPUT_SIZE; i++)
        {
            sum += input_data[i] * w_input_hidden[i][j];
        }
        hidden_raw[j] = sum;
        hidden[j] = leaky_relu(hidden_raw[j]);
    }

    float sum = output_bias[0];
    for (int j = 0; j < HIDDEN_SIZE; j++)
    {
        sum += hidden[j] * w_hidden_output[j][0];
    }

    output[0] = sigmoid(sum);
    return output[0];
}

void nu_ld2410_train(float *input_data, float target)
{
    float pred = nu_ld2410_forward(input_data);
    float error = target - pred;

    float d_output = error * d_sigmoid(pred);

    for (int j = 0; j < HIDDEN_SIZE; j++)
    {
        float d_hidden =
            d_output * w_hidden_output[j][0] * d_leaky_relu(hidden_raw[j]);

        for (int i = 0; i < INPUT_SIZE; i++)
        {
            w_input_hidden[i][j] += LEARNING_RATE * d_hidden * input_data[i];
        }

        w_hidden_output[j][0] += LEARNING_RATE * d_output * hidden[j];

        hidden_bias[j] += LEARNING_RATE * d_hidden;
    }

    output_bias[0] += LEARNING_RATE * d_output;
}

bool nu_ld2410_update(float *sensor_data, int human_present, int is_training)
{
    char ANType = 0;
    float getpred = 0.0;
    float an_upper_threshold = gnuld2410_someone_threshold * 1.4;
    float an_lower_threshold = gnuld2410_noone_threshold / 2;
    ld2410_getANType(&ANType);
    memcpy(sensor_buffer[sensor_index], sensor_data,
           sizeof(float) * SENSOR_SIZE);
    sensor_index = (sensor_index + 1) % TIME_WINDOW;
    sample_count++;
    if (sample_count < TIME_WINDOW) return false;

    /* Build data from sensor for training */
    nu_ld2410_build_input_from_buffer(is_training);

    float target = (float)human_present;
    float pred = nu_ld2410_forward(input);
    // float loss = 0.5f * (target - pred) * (target - pred);
    nu_ld2410_getPred(&getpred);
    if ((getpred < an_upper_threshold) && (pred > an_upper_threshold))
    {
        /* over someone threshold, clear time */
        gnuld2410_less_someoneth_time = 0;
        gnuld2410_less_nooneth_time = 0;
        gnuld2410_keep_nooneth_counter = 0;
    }
    if ((getpred > an_upper_threshold) && (pred < an_upper_threshold))
    {
        /* less someone threshold, record time */
        gnuld2410_less_someoneth_time = esp_timer_get_time();
    }
    if ((getpred > gnuld2410_noone_threshold) &&
        (pred < gnuld2410_noone_threshold))
    {
        /* less noone threshold, record time */
        gnuld2410_less_nooneth_time = esp_timer_get_time();
        gnuld2410_keep_nooneth_counter = 5 * 60 * 2;
    }

    if (gnuld2410_keep_nooneth_counter)
    {
        if (pred < gnuld2410_noone_threshold)
        {
            gnuld2410_keep_nooneth_counter--;
        }
    }
    nu_ld2410_setPred(pred);

    if (is_training)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                       "ann ld2410 update ANType %d", ANType);
        if (ANType)
        {
            nu_ld2410_train(input, target);

            /* Record history data for balance training */
            nu_ld2410_add_sensor_input_to_history(input, human_present);

            /* Balance Training */
            if ((history_noone_count == (HISTORY_SIZE / 2)) && human_present)
            {
                /* Build data from history buffer for balance training */
                nu_ld2410_build_input_from_history(!human_present);
                nu_ld2410_train(input, !human_present);
            }
            if ((history_someone_count == (HISTORY_SIZE / 2)) && !human_present)
            {
                /* Build data from history buffer for balance training */
                nu_ld2410_build_input_from_history(!human_present);
                nu_ld2410_train(input, human_present);
            }
        }

        if (ANType & LD2410_AN_TYPE_STILLNESS)
        {
            an_upper_threshold = gnuld2410_someone_threshold * 1.15;
        }

        /* Auto Complete Learning */
        if (human_present)
        {
            if (pred >= an_upper_threshold)
            {
                gnuld2410_someone_threshold_counter++;
            }
            else
            {
                gnuld2410_someone_threshold_counter = 0;
            }
        }
        else
        {
            if (pred <= an_lower_threshold)
            {
                gnuld2410_noone_threshold_counter++;
            }
            else
            {
                gnuld2410_noone_threshold_counter = 0;
            }
        }

        if (gnuld2410_someone_threshold_counter > 10 ||
            gnuld2410_noone_threshold_counter > 10)
        {
            ld2410_setANType(LD2410_AN_TYPE_NONE);
            gnuld2410_noone_threshold_counter = 0;
            gnuld2410_someone_threshold_counter = 0;
        }
    }

    if (pred > gnuld2410_someone_threshold)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                       "True pred %.2f, istraining %d, someone %d", pred,
                       is_training, human_present);
        return true;
    }

    if (pred < gnuld2410_noone_threshold)
    {
        if (gnuld2410_less_someoneth_time && gnuld2410_less_nooneth_time &&
            ((gnuld2410_less_nooneth_time - gnuld2410_less_someoneth_time) <
             2000000))
        {
            /*
                If prediction from over than someone_threshold to less than
               noone_threshold within 2 seconds turn to non-occupancy
            */
            syslog_handler(
                SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                "False pred %.2f time %d, istraining %d, someone %d", pred,
                (gnuld2410_less_nooneth_time - gnuld2410_less_someoneth_time) /
                    1000000,
                is_training, human_present);
            return false;
        }
        else
        {
            if (gnuld2410_keep_nooneth_counter == 0)
            {
                /*
                    If pred less than noone threshold 2 minutes
                    turn to non-occupancy
                */
                syslog_handler(
                    SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                    "False pred %.2f keepnon %d, istraining %d, someone %d",
                    pred, gnuld2410_keep_nooneth_counter, is_training,
                    human_present);
                return false;
            }
        }
    }
    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG,
                   "Keep pred %.2f IsOcc %d, istraining %d, someone %d", pred,
                   ld2410_isOccupancyStatus(), is_training, human_present);
    return ld2410_isOccupancyStatus();
}

/* NU Weights Upload/Download - Base64 encoding/decoding */

bool nu_ld2410_get_weights_base64(char *w_ih_b64, char *w_ho_b64, char *b_h_b64,
                                  char *b_o_b64)
{
    int ret;
    size_t olen;

    /* Encode w_input_hidden */
    ret = base64_encode(w_ih_b64, 4096, &olen,
                        (const unsigned char *)w_input_hidden,
                        sizeof(float) * INPUT_SIZE * HIDDEN_SIZE);
    if (ret != 0)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Base64 encode w_ih failed: %d", ret);
        return false;
    }

    /* Encode w_hidden_output */
    ret = base64_encode(w_ho_b64, 256, &olen,
                        (const unsigned char *)w_hidden_output,
                        sizeof(float) * HIDDEN_SIZE * OUTPUT_SIZE);
    if (ret != 0)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Base64 encode w_ho failed: %d", ret);
        return false;
    }

    /* Encode hidden_bias */
    ret = base64_encode(b_h_b64, 256, &olen, (const unsigned char *)hidden_bias,
                        sizeof(float) * HIDDEN_SIZE);
    if (ret != 0)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Base64 encode b_h failed: %d", ret);
        return false;
    }

    /* Encode output_bias */
    ret = base64_encode(b_o_b64, 64, &olen, (const unsigned char *)output_bias,
                        sizeof(float) * OUTPUT_SIZE);
    if (ret != 0)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Base64 encode b_o failed: %d", ret);
        return false;
    }

    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_INFO,
                   "Weights exported to Base64");
    return true;
}

bool nu_ld2410_set_weights_from_base64(const char *w_ih_b64,
                                       const char *w_ho_b64,
                                       const char *b_h_b64, const char *b_o_b64)
{
    int ret;
    size_t olen;

    if (gsemaNULD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (nu upload)");
        return false;
    }

    if (xSemaphoreTake(gsemaNULD2410Cfg, portMAX_DELAY) != pdTRUE)
    {
        return false;
    }

    /* Decode w_input_hidden */
    ret = base64_decode((unsigned char *)w_input_hidden,
                        sizeof(float) * INPUT_SIZE * HIDDEN_SIZE, &olen,
                        w_ih_b64, strlen(w_ih_b64));
    if (ret != 0 || olen != sizeof(float) * INPUT_SIZE * HIDDEN_SIZE)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Base64 decode w_ih failed: ret=%d, olen=%d", ret, olen);
        xSemaphoreGive(gsemaNULD2410Cfg);
        return false;
    }

    /* Decode w_hidden_output */
    ret = base64_decode((unsigned char *)w_hidden_output,
                        sizeof(float) * HIDDEN_SIZE * OUTPUT_SIZE, &olen,
                        w_ho_b64, strlen(w_ho_b64));
    if (ret != 0 || olen != sizeof(float) * HIDDEN_SIZE * OUTPUT_SIZE)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Base64 decode w_ho failed: ret=%d, olen=%d", ret, olen);
        xSemaphoreGive(gsemaNULD2410Cfg);
        return false;
    }

    /* Decode hidden_bias */
    ret =
        base64_decode((unsigned char *)hidden_bias, sizeof(float) * HIDDEN_SIZE,
                      &olen, b_h_b64, strlen(b_h_b64));
    if (ret != 0 || olen != sizeof(float) * HIDDEN_SIZE)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Base64 decode b_h failed: ret=%d, olen=%d", ret, olen);
        xSemaphoreGive(gsemaNULD2410Cfg);
        return false;
    }

    /* Decode output_bias */
    ret =
        base64_decode((unsigned char *)output_bias, sizeof(float) * OUTPUT_SIZE,
                      &olen, b_o_b64, strlen(b_o_b64));
    if (ret != 0 || olen != sizeof(float) * OUTPUT_SIZE)
    {
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_ERROR,
                       "Base64 decode b_o failed: ret=%d, olen=%d", ret, olen);
        xSemaphoreGive(gsemaNULD2410Cfg);
        return false;
    }

    xSemaphoreGive(gsemaNULD2410Cfg);

    syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_INFO,
                   "Weights imported from Base64 (not saved to SPIFFS yet)");
    return true;
}
