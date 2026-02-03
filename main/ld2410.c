/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "ld2410.h"
#include "nu_ld2410.h"
#include "syslog.h"
#include "system.h"
#include "rmt.h"
#include "ir_delta_encoder.h"
#include "oled.h"
#include "dht22.h"
#include "homekit.h"
#include "esp_timer.h"
#include "esp_wifi.h"

/* LD2410 commands */
char LD2410_StartCommand[LD2410_StartCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, LD2410_CMD_START_VALUE,
    0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
char LD2410_EndCommand[LD2410_EndCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, LD2410_CMD_END_VALUE,
    0x00, 0x04, 0x03, 0x02, 0x01};
char LD2410_SetDisRateCommand[LD2410_DisRateCommandLen] = {
    0xFD,
    0xFC,
    0xFB,
    0xFA,
    0x04,
    0x00,
    LD2410_CMD_SETDIS_VALUE,
    LD2410_OPT_DISRATE020,
    0x04,
    0x03,
    0x02,
    0x01};
char LD2410_GetParaCommand[LD2410_ReadParaCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, LD2410_CMD_GETPARA_VALUE,
    0x00, 0x04, 0x03, 0x02, 0x01};
char LD2410_SetReplyEngCommand[LD2410_ReplyTypeCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, LD2410_CMD_SETENG_VALUE,
    0x00, 0x04, 0x03, 0x02, 0x01};
char LD2410_SetReplyGenCommand[LD2410_ReplyTypeCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, LD2410_CMD_SETGEN_VALUE,
    0x00, 0x04, 0x03, 0x02, 0x01};
char LD2410_SetDis75Command[LD2410_SetDisCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, LD2410_CMD_SETDIS_VALUE,
    0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
char LD2410_SetDis20Command[LD2410_SetDisCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, LD2410_CMD_SETDIS_VALUE,
    0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
char LD2410_SetMaxDisIdelCommand[LD2410_MaxDisIdelCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x14, 0x00, LD2410_CMD_SETIDL_VALUE,
    0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03,
    0x02, 0x01};
char LD2410_SetSensitivityCommand[LD2410_MaxDisIdelCommandLen] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x14, 0x00, LD2410_CMD_SETSENS_VALUE,
    0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03,
    0x02, 0x01};
int gld2410_dbg_flag = 0;
uint32_t approachcounter = 0;

esp_timer_handle_t gnon_occupancy_delay_timer_handle = NULL;
SemaphoreHandle_t gsemaAutoLearn = NULL;
SemaphoreHandle_t gsemaLD2410Cfg = NULL;
/*
   Chip default
   Door     0,  1,  2,  3,  4,  5,  6,  7,  8
   ActSens 50, 50, 40, 30, 20, 15, 15, 15, 15
   StaSens NA, NA, 40, 40, 30, 30, 20, 20, 20
*/

char gld2410ANType = LD2410_AN_TYPE_NONE;
uint32_t gld2410LeaveDelayTime = LD2410_SW_NON_OCCUPANCY_DELAY;  // Seconds
static const uart_port_t gld2410_uart_num = UART_NUM_2;
static uart_config_t gld2410_uart_config = {
    .baud_rate = 256000,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
};
// Setup UART buffered IO with event queue
static const int guart_buffer_size = (1024 * 2);
static QueueHandle_t gqueue_uart;
static uint8_t gLD2410OccupancyPreStatus =
    false;                               // gLD2410_occupancy_PreStatus
uint8_t gLD2410OccupancyStatus = false;  // gLD2410_IsOccupancy

static void dbg_ld2410_autolearned_data(void);
static void dbg_ld2410_dataraw(uint8_t *data, int length);
static void ld2410_checkdata(uint8_t *data, int length);
static void ld2410_nu_autolearningNobody(uint8_t *data, int length);
static void ld2410_nu_autolearningSomebody(uint8_t *data, int length);
static void ld2410_nu_autolearningStillness(uint8_t *data, int length);
static uint8_t ld2410_nu_checkreply(uint8_t *data, int length);
static void ld2410_restoreconfig(void);
extern esp_timer_handle_t gled_display_timer_handle;
static void dbg_ld2410_autolearned_data(void)
{
    int k = 0;
    dbg_printf("\n\n DBG autolearned data:\n");
    for (k = LD2410_MIN_DIS; k <= LD2410_MAX_DIS; k++)
    {
#if defined(LD2410_AUTOLEARN_REC_MAXPOWER)
        dbg_printf(" AutoLearned Dis%d ActPwr %08d, StaPwr %08d\n", k,
                   gld2410_an_setnb[k][0], gld2410_an_setnb[k][1]);
        dbg_printf(" AutoLearned Dis%d ActPwr %08d, StaPwr %08d\n", k,
                   gld2410_an_setsb[k][0], gld2410_an_setsb[k][1]);
#elif defined(LD2410_AUTOLEARN_REC_MAXCOUNTER)
        dbg_printf(" Analysis NoBody\n");
        for (int kk = 0; kk <= 100; kk++)
        {
            if (gld2410_an_nbact[kk][k] + gld2410_an_nbsta[kk][k] > 0)
            {
                dbg_printf(" %03d %06lu %06lu\n", kk, gld2410_an_nbact[kk][k],
                           gld2410_an_nbsta[kk][k]);
            }
        }
        dbg_printf(" Analysis SoBody\n");
        for (kk = 0; kk <= 100; kk++)
        {
            if (gld2410_an_sbact[kk][k] + gld2410_an_sbsta[kk][k] > 0)
            {
                dbg_printf(" %03d %06lu %06lu\n", kk, gld2410_an_sbact[kk][k],
                           gld2410_an_sbsta[kk][k]);
            }
        }
#endif
    }
    dbg_printf("\n -------- end -------\n\n");
}

static void dbg_ld2410_dataraw(uint8_t *data, int length)
{
    dbg_printf("\n\n DBG LD2410 raw data:\n");
    dbg_printf("--- 01 02 03 04  05 06 07 08 ---\n");
    for (int i = 0; i < length; i++)
    {
        switch (i % 8)
        {
            case 0:
                dbg_printf("    %02x", data[i]);
                break;
            case 4:
                dbg_printf("  %02x", data[i]);
                break;
            case 7:
                dbg_printf(" %02x\n", data[i]);
                break;
            default:
                dbg_printf(" %02x", data[i]);
        }
    }
    dbg_printf("\n -------- end -------\n\n");
}

int ld2410_getDebuggingMode(int *flag)
{
    if (gsemaLD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (ld2410 %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaLD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        *flag = gld2410_dbg_flag;
        xSemaphoreGive(gsemaLD2410Cfg);
    }
    return true;
}

int ld2410_setDebuggingMode(int flag)
{
    if (gsemaLD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (ld2410 %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaLD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        gld2410_dbg_flag = flag;
        xSemaphoreGive(gsemaLD2410Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int ld2410_getLeaveDelayTime(uint32_t *time)
{
    if (gsemaLD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (ld2410 %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaLD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        *time = gld2410LeaveDelayTime;
        xSemaphoreGive(gsemaLD2410Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int ld2410_setLeaveDelayTime(uint32_t time)
{
    if (gsemaLD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (ld2410 %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaLD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        gld2410LeaveDelayTime = time;
        xSemaphoreGive(gsemaLD2410Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int ld2410_getANType(char *type)
{
    if (gsemaLD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (ld2410 %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaLD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        *type = gld2410ANType;
        xSemaphoreGive(gsemaLD2410Cfg);
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG, "Set ANType %d",
                       *type);
    }
    return SYSTEM_ERROR_NONE;
}

int ld2410_setANType(char type)
{
    if (gsemaLD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (ld2410 %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaLD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        gld2410ANType = type;
        xSemaphoreGive(gsemaLD2410Cfg);
        syslog_handler(SYSLOG_FACILITY_ANN, SYSLOG_LEVEL_DEBUG, "Set ANType %d",
                       gld2410ANType);
    }
    return SYSTEM_ERROR_NONE;
}

bool ld2410_isOccupancyStatus()
{
    bool ret = false;

    if (gsemaLD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (ld2410 %d)", __LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaLD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        ret = gLD2410OccupancyStatus;
        xSemaphoreGive(gsemaLD2410Cfg);
    }
    return ret;
}

int ld2410_setOccupancyStatus(bool status)
{
    if (gsemaLD2410Cfg == NULL)
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_ERROR,
                       "Semaphore not ready (ld2410 %d)", __LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaLD2410Cfg, portMAX_DELAY) == pdTRUE)
    {
        gLD2410OccupancyStatus = status;
        xSemaphoreGive(gsemaLD2410Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

static void ld2410_nu_autolearningNobody(uint8_t *data, int length)
{
    int k = 0;
    float data_with_ld2410[SENSOR_SIZE] = {0};
    int index = 0;
    float temperature = 0, humidity = 0;
    // dbg_printf(" Autolearn NoBody on %s
    // mode\n",data[LD2410_RPLY_TYPE_OFFSET]==0x1?"ENG":"GEN");
    if (data[LD2410_RPLY_TYPE_OFFSET] == LD2410_RPLY_TYPE_ENG)
    {
        for (k = LD2410_MIN_DIS; k <= LD2410_MAX_DIS; k++)
        {
            data_with_ld2410[k + index] =
                data[LD2410_RPLY_MAXACTDIS_OFFSET + 2 + k];
            data_with_ld2410[k + index + 1] =
                data[LD2410_RPLY_MAXACTDIS_OFFSET + 11 + k];
            index++;
        }
        dht22_getcurrenttemperature(&temperature);
        dht22_getcurrenthumidity(&humidity);
        data_with_ld2410[k + index] = temperature;
        data_with_ld2410[k + index + 1] = humidity;
        nu_ld2410_update(data_with_ld2410, 0, 1);
    }
}

static void ld2410_nu_autolearningSomebody(uint8_t *data, int length)
{
    int k = 0;
    float data_with_ld2410[SENSOR_SIZE] = {0};
    int index = 0;
    float temperature = 0, humidity = 0;
    // dbg_printf(" Autolearn SomeBody on %s
    // mode\n",data[LD2410_RPLY_TYPE_OFFSET]==0x1?"ENG":"GEN");
    if (data[LD2410_RPLY_TYPE_OFFSET] == LD2410_RPLY_TYPE_ENG)
    {
        for (k = LD2410_MIN_DIS; k <= LD2410_MAX_DIS; k++)
        {
            data_with_ld2410[k + index] =
                data[LD2410_RPLY_MAXACTDIS_OFFSET + 2 + k];
            data_with_ld2410[k + index + 1] =
                data[LD2410_RPLY_MAXACTDIS_OFFSET + 11 + k];
            index++;
        }
        dht22_getcurrenttemperature(&temperature);
        dht22_getcurrenthumidity(&humidity);
        data_with_ld2410[k + index] = temperature;
        data_with_ld2410[k + index + 1] = humidity;
        nu_ld2410_update(data_with_ld2410, 1, 1);
    }
}

static void ld2410_nu_autolearningStillness(uint8_t *data, int length)
{
    int k = 0;
    float data_with_ld2410[SENSOR_SIZE] = {0};
    int index = 0;
    float temperature = 0, humidity = 0;
    // dbg_printf(" Autolearn SomeBodyMove on %s
    // mode\n",data[LD2410_RPLY_TYPE_OFFSET]==0x1?"ENG":"GEN");
    if (data[LD2410_RPLY_TYPE_OFFSET] == LD2410_RPLY_TYPE_ENG)
    {
        for (k = LD2410_MIN_DIS; k <= LD2410_MAX_DIS; k++)
        {
            data_with_ld2410[k + index] =
                data[LD2410_RPLY_MAXACTDIS_OFFSET + 2 + k];
            data_with_ld2410[k + index + 1] =
                data[LD2410_RPLY_MAXACTDIS_OFFSET + 11 + k];
            index++;
        }
        dht22_getcurrenttemperature(&temperature);
        dht22_getcurrenthumidity(&humidity);
        data_with_ld2410[k + index] = temperature;
        data_with_ld2410[k + index + 1] = humidity;
        nu_ld2410_update(data_with_ld2410, 1, 1);
    }
}

static void ld2410_checkdata(uint8_t *data, int length)
{
    uint32_t patten = 0;
    uint32_t cmdrply = 0xFDFCFBFA;
    uint32_t starply = 0xF4F3F2F1;
    patten = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
    // dbg_ld2410_dataraw(data, length);
    if (patten == starply)
    {
        ld2410_updatestatus(data, length);
    }
    if (patten == cmdrply)
    {
        if (data[7] != 0x01)
        {
            switch (data[6])
            {
                case LD2410_CMD_START_VALUE:
                    dbg_printf(" Cmd START rply %02x\n", data[7]);
                    break;
                case LD2410_CMD_END_VALUE:
                    dbg_printf(" Cmd END rply %02x\n", data[7]);
                    break;
                case LD2410_CMD_SETDIS_VALUE:
                    dbg_printf(" Cmd SET DISTANCE rply %02x\n", data[7]);
                    break;
                case LD2410_CMD_GETPARA_VALUE:
                    dbg_printf(" Cmd GET PARA rply %02x\n", data[7]);
                    break;
                case LD2410_CMD_SETENG_VALUE:
                    dbg_printf(" Cmd SET ENGINEER MODE rply %02x\n", data[7]);
                    break;
                case LD2410_CMD_SETGEN_VALUE:
                    dbg_printf(" Cmd SET GENERAL MODE rply %02x\n", data[7]);
                    break;
                case LD2410_CMD_SETIDL_VALUE:
                    dbg_printf(" Cmd SET IDEL rply %02x\n", data[7]);
                    break;
                case LD2410_CMD_SETSENS_VALUE:
                    dbg_printf(" Cmd SET SENCITIVY rply %02x\n", data[7]);
                    break;
                default:
                    dbg_printf(" Cmd UNKNOWN rply %02x\n", data[7]);
                    break;
            }
        }
    }
}

static uint8_t ld2410_nu_checkreply(uint8_t *data, int length)
{
    int k = 0;
    uint8_t status = 0;
    esp_timer_create_args_t led_display_timer_args = {
        .callback = &led_display_app_timer_callback,
        .name = "led_display_timer"};
    float data_with_ld2410[SENSOR_SIZE] = {0};
    int index = 0;
    float temperature = 0, humidity = 0;
    char ANType = 0;
    int leddisplaytime = 0;

    ld2410_getANType(&ANType);

    if (data[LD2410_RPLY_TYPE_OFFSET] == LD2410_RPLY_TYPE_ENG)
    {
        if (gld2410_dbg_flag)
        {
            syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_DEBUG,
                           "----------------");
        }
        for (k = LD2410_MIN_DIS; k <= LD2410_MAX_DIS; k++)
        {
            if (k == 0)
            {
                uint8_t val = data[LD2410_RPLY_MAXACTDIS_OFFSET + 2 + k];
                if (val >= (90))
                {
                    if (gsemaLED != NULL)
                    {
                        if (xSemaphoreTake(gsemaLED, portMAX_DELAY) == pdTRUE)
                        {
                            oled_setDisplayMode(LED_DISPLAY_MODE_TIME);
                            xSemaphoreGive(gsemaLED);
                        }
                        if (gled_display_timer_handle != NULL)
                        {
                            if (esp_timer_is_active(gled_display_timer_handle))
                            {
                                ESP_ERROR_CHECK(
                                    esp_timer_stop(gled_display_timer_handle));
                            }
                        }
                        else
                        {
                            ESP_ERROR_CHECK(
                                esp_timer_create(&led_display_timer_args,
                                                 &gled_display_timer_handle));
                        }
                        oled_getDisplayTime(&leddisplaytime);
                        ESP_ERROR_CHECK(esp_timer_start_once(
                            gled_display_timer_handle,
                            (leddisplaytime * 1000 * 1000)));
                    }
                }
            }
            if (ANType == LD2410_AN_TYPE_NONE)
            {
                data_with_ld2410[k + index] =
                    data[LD2410_RPLY_MAXACTDIS_OFFSET + 2 + k];
                data_with_ld2410[k + index + 1] =
                    data[LD2410_RPLY_MAXACTDIS_OFFSET + 11 + k];
                index++;
            }
        }
        if (ANType == LD2410_AN_TYPE_NONE)
        {
            dht22_getcurrenttemperature(&temperature);
            dht22_getcurrenthumidity(&humidity);
            data_with_ld2410[k + index] = temperature;
            data_with_ld2410[k + index + 1] = humidity;
            status = (uint8_t)nu_ld2410_update(data_with_ld2410, 0, 0);
        }
    }
    else
    {
        syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_DEBUG,
                       "LD2410 Reply in general mode");
    }
    return status;
}

static void ld2410_restoreconfig(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    uint32_t value1 = 0;

    gsemaLD2410Cfg = xSemaphoreCreateBinary();
    if (gsemaLD2410Cfg == NULL)
    {
        return;
    }
    ret = nvs_open(LD2410_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(gsemaLD2410Cfg);
        return;
    }

    ret = nvs_get_u32(nvs_handle, LD2410_NVS_LEARNSTATUS_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gld2410LeaveDelayTime = value1;
    }
    else
    {
        ESP_LOGE(TAG_NVS, "NVS set failed for key-key: %s",
                 esp_err_to_name(ret));
    }
    nvs_close(nvs_handle);
    xSemaphoreGive(gsemaLD2410Cfg);
    return;
}

void ld2410_saveconfig(char *key, uint32_t data)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(LD2410_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        return;
    }

    uint32_t value1 = data;
    ret = nvs_set_u32(nvs_handle, key, value1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "NVS set failed for key1: %s", esp_err_to_name(ret));
    }
    nvs_close(nvs_handle);
    syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_INFO,
                   "Config saved %s %d", key, data);
    return;
}

/* LD2410 driver */
void ld2410_setend(int delay)
{
    uart_write_bytes(gld2410_uart_num, LD2410_EndCommand,
                     sizeof(LD2410_EndCommand));
    vTaskDelay(delay / portTICK_PERIOD_MS);
}

void ld2410_setmaxdisidel(uint32_t maxactdis, uint32_t maxstadis,
                          uint32_t maxidel)
{
    LD2410_SetMaxDisIdelCommand[10] = maxactdis % 256;
    LD2410_SetMaxDisIdelCommand[11] = (maxactdis >> 8) % 256;
    LD2410_SetMaxDisIdelCommand[12] = (maxactdis >> 16) % 256;
    LD2410_SetMaxDisIdelCommand[13] = (maxactdis >> 24) % 256;
    LD2410_SetMaxDisIdelCommand[16] = maxstadis % 256;
    LD2410_SetMaxDisIdelCommand[17] = (maxstadis >> 8) % 256;
    LD2410_SetMaxDisIdelCommand[18] = (maxstadis >> 16) % 256;
    LD2410_SetMaxDisIdelCommand[19] = (maxstadis >> 24) % 256;
    LD2410_SetMaxDisIdelCommand[22] = maxidel % 256;
    LD2410_SetMaxDisIdelCommand[23] = (maxidel >> 8) % 256;
    LD2410_SetMaxDisIdelCommand[24] = (maxidel >> 16) % 256;
    LD2410_SetMaxDisIdelCommand[25] = (maxidel >> 24) % 256;
    dbg_printf(" Set MaxActDis %02x, %02x, %02x, %02x\n",
               LD2410_SetMaxDisIdelCommand[10], LD2410_SetMaxDisIdelCommand[11],
               LD2410_SetMaxDisIdelCommand[12],
               LD2410_SetMaxDisIdelCommand[13]);
    dbg_printf(" Set MaxStaDis %02x, %02x, %02x, %02x\n",
               LD2410_SetMaxDisIdelCommand[16], LD2410_SetMaxDisIdelCommand[17],
               LD2410_SetMaxDisIdelCommand[18],
               LD2410_SetMaxDisIdelCommand[19]);
    dbg_printf(" Set MaxIdel   %02x, %02x, %02x, %02x\n",
               LD2410_SetMaxDisIdelCommand[22], LD2410_SetMaxDisIdelCommand[23],
               LD2410_SetMaxDisIdelCommand[24],
               LD2410_SetMaxDisIdelCommand[25]);
    uart_write_bytes(gld2410_uart_num, LD2410_SetMaxDisIdelCommand,
                     sizeof(LD2410_SetMaxDisIdelCommand));
    vTaskDelay(LD2410_INTERNALDELAY / portTICK_PERIOD_MS);
}

void ld2410_setreplyeng(void)
{
    uart_write_bytes(gld2410_uart_num, LD2410_SetReplyEngCommand,
                     sizeof(LD2410_SetReplyEngCommand));
    dbg_printf(" Set Reply engineering information\n");
    vTaskDelay(LD2410_INTERNALDELAY / portTICK_PERIOD_MS);
}

void ld2410_setreplygen(void)
{
    uart_write_bytes(gld2410_uart_num, LD2410_SetReplyGenCommand,
                     sizeof(LD2410_SetReplyGenCommand));
    dbg_printf(" Set Reply general information\n");
    vTaskDelay(LD2410_INTERNALDELAY / portTICK_PERIOD_MS);
}

void ld2410_setdis20(void)
{
    uart_write_bytes(gld2410_uart_num, LD2410_SetDis20Command,
                     sizeof(LD2410_SetDis20Command));
    dbg_printf(" Set Door Distance 20cm\n");
    vTaskDelay(LD2410_INTERNALDELAY / portTICK_PERIOD_MS);
}

void ld2410_setdis75(void)
{
    uart_write_bytes(gld2410_uart_num, LD2410_SetDis75Command,
                     sizeof(LD2410_SetDis75Command));
    dbg_printf(" Set Door Distance 75cm\n");
    vTaskDelay(LD2410_INTERNALDELAY / portTICK_PERIOD_MS);
}

void ld2410_setSensitivity(uint32_t disdoor, uint32_t activesensitivity,
                           uint32_t staticsensitivity)
{
    if ((disdoor > LD2410_CFG_MAXSTADIS) ||
        (activesensitivity > LD2410_CFG_MAXSENSITIVITY) ||
        (staticsensitivity > LD2410_CFG_MAXSENSITIVITY))
    {
        return;
    }
    LD2410_SetSensitivityCommand[10] = disdoor;
    LD2410_SetSensitivityCommand[11] = 0;
    LD2410_SetSensitivityCommand[12] = 0;
    LD2410_SetSensitivityCommand[13] = 0;
    LD2410_SetSensitivityCommand[14] = 0x01;
    LD2410_SetSensitivityCommand[15] = 0;
    LD2410_SetSensitivityCommand[16] = activesensitivity;
    LD2410_SetSensitivityCommand[17] = 0;
    LD2410_SetSensitivityCommand[18] = 0;
    LD2410_SetSensitivityCommand[19] = 0;
    LD2410_SetSensitivityCommand[20] = 0x02;
    LD2410_SetSensitivityCommand[21] = 0;
    LD2410_SetSensitivityCommand[22] = staticsensitivity;
    LD2410_SetSensitivityCommand[23] = 0;
    LD2410_SetSensitivityCommand[24] = 0;
    LD2410_SetSensitivityCommand[25] = 0;
    dbg_printf("\n Set Door %02d, ActSen %02d, StaSen %02d\n",
               LD2410_SetSensitivityCommand[10],
               LD2410_SetSensitivityCommand[16],
               LD2410_SetSensitivityCommand[22]);
    uart_write_bytes(gld2410_uart_num, LD2410_SetSensitivityCommand,
                     sizeof(LD2410_SetSensitivityCommand));
    vTaskDelay(LD2410_INTERNALDELAY / portTICK_PERIOD_MS);
}

void ld2410_setstart(int delay)
{
    uart_write_bytes(gld2410_uart_num, LD2410_StartCommand,
                     sizeof(LD2410_StartCommand));
    ESP_ERROR_CHECK(uart_wait_tx_done(
        gld2410_uart_num, 100));  // wait timeout is 100 RTOS ticks (TickType_t)
    vTaskDelay(delay / portTICK_PERIOD_MS);
}

void ld2410_uart_init(void)
{
    // Configure UART parameters
    dbg_printf(" Configure UART parameters...\n");
    ESP_ERROR_CHECK(uart_param_config(gld2410_uart_num, &gld2410_uart_config));

    // Set UART pins(TX: I17, RX: I16, RTS: IO18, CTS: IO19)
    dbg_printf(" Set UART pins(TX: I17, RX: I16, RTS: -1, CTS: -1)...\n");
    ESP_ERROR_CHECK(uart_set_pin(gld2410_uart_num, TO_LD2410_GPIO_NUM,
                                 FROM_LD2410_GPIO_NUM, -1, -1));

    // Install UART driver using an event queue here
    dbg_printf(" Install UART driver w/ an event queue...\n");
    ESP_ERROR_CHECK(uart_driver_install(gld2410_uart_num, guart_buffer_size,
                                        guart_buffer_size, 64, &gqueue_uart,
                                        0));

    // Set uart pattern detect function.
    uart_enable_pattern_det_baud_intr(gld2410_uart_num, '+', PATTERN_CHR_NUM, 9,
                                      0, 0);

    // Reset the pattern queue length to record at most 20 pattern positions.
    uart_pattern_queue_reset(gld2410_uart_num, 20);

    /* Configure LD2410 */
    ld2410_setstart(LD2410_INTERNALDELAY);
}

void ld2410_updatestatus(uint8_t *data, int length)
{
    uint8_t ObjCurStatus = 0;  // data[k+LD2410_RPLY_STAUS_OFFSET];
    rmt_zftg_msg_t msg;
    uint8_t sys_mac[6];
    char ANType = 0;

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    memset(&msg, 0, sizeof(rmt_zftg_msg_t));
    esp_timer_create_args_t non_occupancy_delay_timer_args = {
        .callback = &non_occupancy_delay_timer_callback,
        .name = "nonocc_timer"};
    esp_timer_create_args_t sync_delta_warm_timer_args = {
        .callback = &ir_sync_delta_warm_timer_callback,
        .name = "sync_delta_warm_timer"};
    float temperature = 0, humidity = 0;
    int humihigh = 0, templow = 0;

    ld2410_getANType(&ANType);
    if (gsemaAutoLearn != NULL)
    {
        if (ANType & LD2410_AN_TYPE_NOONE)
        {
            if (xSemaphoreTake(gsemaAutoLearn, portMAX_DELAY) == pdTRUE)
            {
#if defined(LD2410_AUTOLEARN_NU)
                ld2410_nu_autolearningNobody(data, length);
#endif
                xSemaphoreGive(gsemaAutoLearn);
                return;
            }
        }
        if (ANType & LD2410_AN_TYPE_SOMEONE)
        {
            if (xSemaphoreTake(gsemaAutoLearn, portMAX_DELAY) == pdTRUE)
            {
#if defined(LD2410_AUTOLEARN_NU)
                ld2410_nu_autolearningSomebody(data, length);
#endif
                xSemaphoreGive(gsemaAutoLearn);
                return;
            }
        }
        if (ANType & LD2410_AN_TYPE_STILLNESS)
        {
            if (xSemaphoreTake(gsemaAutoLearn, portMAX_DELAY) == pdTRUE)
            {
#if defined(LD2410_AUTOLEARN_NU)
                ld2410_nu_autolearningStillness(data, length);
#endif
                xSemaphoreGive(gsemaAutoLearn);
                return;
            }
        }
    }
#if defined(LD2410_AUTOLEARN_NU)
    ObjCurStatus = ld2410_nu_checkreply(data, length);
#endif

    if (((ObjCurStatus || LD2410_EXIST) &&
         (ld2410_isOccupancyStatus() != true)) ||
        ((ObjCurStatus == 0x00) && (ld2410_isOccupancyStatus() != false)))
    {
        // ld2410_setOccupancyPreStatus(ObjCurStatus);
        int tigger_occupancy = true;
        if ((ObjCurStatus && LD2410_EXIST) &&
            (ld2410_isOccupancyStatus() != true))
        {
            ld2410_setOccupancyStatus(true);

            if (hap_iselfactive())  // If elf is enabled
            {
                if (gnon_occupancy_delay_timer_handle !=
                    NULL)  // During non occupancy delay time, stop timer
                {
                    if (esp_timer_is_active(gnon_occupancy_delay_timer_handle))
                    {
                        ESP_ERROR_CHECK(
                            esp_timer_stop(gnon_occupancy_delay_timer_handle));
                        tigger_occupancy =
                            false;  // Homekit status is occupancy, don't have
                                    // to update.
                    }
                }

                if (tigger_occupancy)  // Update homekit status
                {
                    int value = 0;
                    value = ld2410_isOccupancyStatus();
                    hap_update_value(HAP_ACCESSORY_OCCUPANCY,
                                     HAP_CHARACTER_IGNORE, &value);
                    if (!IS_BATHROOM(sys_mac))
                    {
                        if (hap_iselfoccupancyfanactive() !=
                            rmt_iszerofanactive())
                        {
                            msg.bactivech = true;
                            msg.active = 1;
                            if (ir_zerofan_tigger(msg) == SYSTEM_ERROR_NONE)
                            {
                                syslog_handler(
                                    SYSLOG_FACILITY_ELF, SYSLOG_LEVEL_INFO,
                                    "Recover +-0 Fan status from %s to %s",
                                    rmt_iszerofanactive() ? "on" : "off",
                                    hap_iselfoccupancyfanactive() ? "on"
                                                                  : "off");
                                /* If fan is turn on when occupancy */
                                bool active = 0;
                                active = hap_iselfoccupancyfanactive();
                                rmt_setzerofanstatus(active);
                                zerofan_saveconfig(ZEROFAN_NVS_STATUS_KEY,
                                                   active);
                                hap_update_value(HAP_ACCESSORY_ZERO_FAN,
                                                 HAP_CHARACTER_ACTIVE, &active);
                            }
                        }
                    }
                    syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_INFO,
                                   "Detect someone");
                    dht22_getcurrenttemperature(&temperature);
                    dht22_getcurrenthumidity(&humidity);
                    dht22_getlowtemperature(&templow);
                    dht22_gethighhumidity(&humihigh);
                    if ((humidity >= humihigh) && (temperature <= templow) &&
                        (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac)))
                    {
                        /* Detect People and humidity high (shower) and
                         * temperature low (cold) turn on delta warm fan */
                        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,
                                       SYSLOG_LEVEL_INFO,
                                       "Shower and temperature is cold %.1f, "
                                       "turn on warm fan",
                                       temperature, humidity);
                        ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_WARM,
                                           IR_DELTA_FAN_TIGGER_ACTIVE_ON,
                                           IR_DELTA_FAN_DURATION_1HR);
                        if (gsync_delta_warm_timer_handle != NULL)
                        {
                            if (esp_timer_is_active(
                                    gsync_delta_warm_timer_handle))
                            {
                                syslog_handler(SYSLOG_FACILITY_OCCUPANCY,
                                               SYSLOG_LEVEL_DEBUG,
                                               "Stop warm timer");
                                ESP_ERROR_CHECK(esp_timer_stop(
                                    gsync_delta_warm_timer_handle));
                            }
                        }
                        else
                        {
                            syslog_handler(SYSLOG_FACILITY_OCCUPANCY,
                                           SYSLOG_LEVEL_DEBUG,
                                           "Create warm timer");
                            ESP_ERROR_CHECK(esp_timer_create(
                                &sync_delta_warm_timer_args,
                                &gsync_delta_warm_timer_handle));
                        }
                        ESP_ERROR_CHECK(esp_timer_start_once(
                            gsync_delta_warm_timer_handle,
                            ((uint64_t)IR_DELTA_FAN_DURATION_1HR * 60 * 60 *
                             1000 * 1000)));
                        syslog_handler(SYSLOG_FACILITY_OCCUPANCY,
                                       SYSLOG_LEVEL_DEBUG,
                                       "Start warm timer %llu",
                                       ((uint64_t)IR_DELTA_FAN_DURATION_1HR) *
                                           60 * 60 * 1000 * 1000);
                        rmt_setwarmfanstatus(true);
                    }
                }
            }
            /* If elf is disabled, don't update the occupancy status.  */
        }
        else if ((ObjCurStatus == 0x00) && (ld2410_isOccupancyStatus() != 0))
        {
            ld2410_setOccupancyStatus(false);
            if (hap_iselfactive())
            {
                uint32_t delaytime = 0;
                if (gnon_occupancy_delay_timer_handle != NULL)
                {
                    if (esp_timer_is_active(gnon_occupancy_delay_timer_handle))
                    {
                        ESP_ERROR_CHECK(
                            esp_timer_stop(gnon_occupancy_delay_timer_handle));
                    }
                }
                else
                {
                    ESP_ERROR_CHECK(
                        esp_timer_create(&non_occupancy_delay_timer_args,
                                         &gnon_occupancy_delay_timer_handle));
                }
                // Start non occupancy delay timer
                ld2410_getLeaveDelayTime(&delaytime);
                ESP_ERROR_CHECK(
                    esp_timer_start_once(gnon_occupancy_delay_timer_handle,
                                         (delaytime * 1000 * 1000)));
            }
        }
    }
}

void task_ld2410(void *pvParameter)
{
    int iidel_time = LD2410_SW_CFG_IDELTIME;
    int idetect_time = LD2410_SW_CFG_DETECTTIME;
    uint8_t sys_mac[6];
    char ANType = 0;

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
#if defined(LD2410_AUTOLEARN_NU)
    if (nu_ld2410_restoreweights() == false)
    {
        dbg_printf(" Loading NU model failure, init random model\n");
        nu_ld2410_init_weights();
    }
    else
    {
        dbg_printf(" Loading NU model sucess\n");
    }
#endif
    ld2410_restoreconfig();
    // dbg_ld2410_autolearned_data();
    ld2410_setreplyeng();
    if (IS_BATHROOM(sys_mac))
    {
        ld2410_setdis20();
    }
    else
    {
        ld2410_setdis75();
    }
    ld2410_setmaxdisidel(LD2410_CFG_MAXACTDIS, LD2410_CFG_MAXSTADIS,
                         LD2410_CFG_MAXIDEL);
    // ld2410_setend(LD2410_SW_CFG_DETECTTIME); /* Start */
    system_task_created(TASK_LD2410_ID);
    system_task_all_ready();

    while (1)
    {
        ld2410_getANType(&ANType);
        if (ld2410_isOccupancyStatus() || ANType)
        {
            /* Noone or auto learning */
            iidel_time = LD2410_SW_CFG_IDELTIME;
            idetect_time = LD2410_SW_CFG_DETECTTIME;
        }
        else
        {
            /* Someone */
            iidel_time = LD2410_SW_CFG_IDELTIME;
            idetect_time = LD2410_SW_CFG_DETECTTIME;
        }
        ld2410_setstart(iidel_time); /* Stop */
        if (gsemaLD2410 != NULL)
        {
            if (xSemaphoreTake(gsemaLD2410, portMAX_DELAY) == pdTRUE)
            {
                /* Idel Time */
                ld2410_setend(idetect_time); /* Start */
                xSemaphoreGive(gsemaLD2410);
                /* Detect Time */
            }
        }
    }
    vTaskDelete(NULL);
}

void task_uart_event(void *pvParameters)
{
    uart_event_t event;
    size_t buffered_size;
    uint8_t *dtmp = (uint8_t *)malloc(guart_buffer_size);
    gsemaAutoLearn = xSemaphoreCreateBinary();
    if (gsemaAutoLearn != NULL)
    {
        xSemaphoreGive(gsemaAutoLearn);
    }
    system_task_created(TASK_UART_ID);
    while (system_task_is_ready(TASK_LD2410_ID) != SYSTEM_TASK_INIT_DONE)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    system_task_all_ready();

    for (;;)
    {
        // Waiting for UART event.
        if (xQueueReceive(gqueue_uart, (void *)&event,
                          (TickType_t)portMAX_DELAY))
        {
            bzero(dtmp, guart_buffer_size);
            ESP_LOGI(TAG_LD2410, "uart[%d] event:", gld2410_uart_num);
            switch (event.type)
            {
                // Event of UART receving data
                /*We'd better handler data event fast, there would be much more
                data events than other types of events. If we take too much time
                on data event, the queue might be full.*/
                case UART_DATA:
                    ESP_LOGI(TAG_LD2410, "[UART DATA]: %d", event.size);
                    uart_read_bytes(gld2410_uart_num, dtmp, event.size,
                                    portMAX_DELAY);
                    // ESP_LOGI(TAG_LD2410, "[DATA EVT]:");
                    // app_ld2410_readstatus(i);
                    ld2410_checkdata(dtmp, event.size);
                    // uart_write_bytes(gld2410_uart_num, (const char*) dtmp,
                    // event.size);
                    break;
                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    // ESP_LOGI(TAG_LD2410, "hw fifo overflow");
                    //  If fifo overflow happened, you should consider adding
                    //  flow control for your application. The ISR has already
                    //  reset the rx FIFO, As an example, we directly flush the
                    //  rx buffer here in order to read more data.
                    uart_flush_input(gld2410_uart_num);
                    xQueueReset(gqueue_uart);
                    break;
                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    // ESP_LOGI(TAG_LD2410, "ring buffer full");
                    //  If buffer full happened, you should consider increasing
                    //  your buffer size As an example, we directly flush the rx
                    //  buffer here in order to read more data.
                    uart_flush_input(gld2410_uart_num);
                    xQueueReset(gqueue_uart);
                    break;
                // Event of UART RX break detected
                case UART_BREAK:
                    // ESP_LOGI(TAG_LD2410, "uart rx break");
                    break;
                // Event of UART parity check error
                case UART_PARITY_ERR:
                    // ESP_LOGI(TAG_LD2410, "uart parity error");
                    break;
                // Event of UART frame error
                case UART_FRAME_ERR:
                    // ESP_LOGI(TAG_LD2410, "uart frame error");
                    break;
                // UART_PATTERN_DET
                case UART_PATTERN_DET:
                    uart_get_buffered_data_len(gld2410_uart_num,
                                               &buffered_size);
                    int pos = uart_pattern_pop_pos(gld2410_uart_num);
                    // ESP_LOGI(TAG_LD2410, "[UART PATTERN DETECTED] pos: %d,
                    // buffered size: %d", pos, buffered_size);
                    if (pos == -1)
                    {
                        // There used to be a UART_PATTERN_DET event, but the
                        // pattern position queue is full so that it can not
                        // record the position. We should set a larger queue
                        // size. As an example, we directly flush the rx buffer
                        // here.
                        uart_flush_input(gld2410_uart_num);
                    }
                    else
                    {
                        uart_read_bytes(gld2410_uart_num, dtmp, pos,
                                        100 / portTICK_PERIOD_MS);
                        uint8_t pat[PATTERN_CHR_NUM + 1];
                        memset(pat, 0, sizeof(pat));
                        uart_read_bytes(gld2410_uart_num, pat, PATTERN_CHR_NUM,
                                        100 / portTICK_PERIOD_MS);
                        // ESP_LOGI(TAG_LD2410, "read data: %s", dtmp);
                        // ESP_LOGI(TAG_LD2410, "read pat : %s", pat);
                    }
                    break;
                // Others
                default:
                    // ESP_LOGI(TAG_LD2410, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void non_occupancy_delay_timer_callback()
{
    uint8_t sys_mac[6];
    int value = 0;

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    value = ld2410_isOccupancyStatus();
    hap_update_value(HAP_ACCESSORY_OCCUPANCY, HAP_CHARACTER_IGNORE, &value);

    syslog_handler(SYSLOG_FACILITY_OCCUPANCY, SYSLOG_LEVEL_INFO,
                   "Detect no one");
    if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
    {
        if (rmt_iswarmfanactive())
        {
            rmt_setwarmfanstatus(false);
            syslog_handler(SYSLOG_FACILITY_TEMPERATURE, SYSLOG_LEVEL_INFO,
                           "Shower complete, turn off warm fan");
            ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_WARM,
                               IR_DELTA_FAN_TIGGER_ACTIVE_OFF,
                               IR_DELTA_FAN_DURATION_1HR);
            if (gsync_delta_warm_timer_handle != NULL)
            {
                if (esp_timer_is_active(gsync_delta_warm_timer_handle))
                {
                    ESP_ERROR_CHECK(
                        esp_timer_stop(gsync_delta_warm_timer_handle));
                }
            }
        }
    }
    /* If elf is disabled, don't update the occupancy status.  */
}
