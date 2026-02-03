/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "webpages.h"
#include "dht22.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "homekit.h"
#include "ld2410.h"
#include "airquality.h"
#include "nu_ld2410.h"
#include "oled.h"
#include "ota.h"
#include "rmt.h"
#include "sdkconfig.h"
#include "syslog.h"
#include "system.h"
#include "thingspeak.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static int handle_json_get_int(char *src, int *dst, char *obj);
static int handle_json_get_str(char *src, char *dst, int len, char *obj);
static esp_err_t http_api_autolearn_clear(httpd_req_t *req);
static esp_err_t http_api_autolearn_nobody(httpd_req_t *req);
static esp_err_t http_api_autolearn_save(httpd_req_t *req);
static esp_err_t http_api_autolearn_stillness(httpd_req_t *req);
static esp_err_t http_api_autolearn_somebody(httpd_req_t *req);
static esp_err_t http_api_autolearn_stop(httpd_req_t *req);
static esp_err_t http_api_dbgnobody(httpd_req_t *req);
static esp_err_t http_api_dbgoff(httpd_req_t *req);
static esp_err_t http_api_dbgsomebody(httpd_req_t *req);
static esp_err_t http_api_erasedata(httpd_req_t *req);
static esp_err_t http_api_loading(httpd_req_t *req);
static esp_err_t http_api_reboot(httpd_req_t *req);
static esp_err_t http_api_env_updt(httpd_req_t *req);
static esp_err_t http_api_reset_baseline(httpd_req_t *req);
static int http_printf_end(httpd_req_t *req);
static int http_printf(httpd_req_t *req, const char *fmt, ...);
static esp_err_t handle_nu_upload(httpd_req_t *req);
// HTTP GET handler for fetching data
esp_err_t fetch_vue(httpd_req_t *req)
{
    char param[32];
    esp_err_t ret;
    int ota_msg = 0;
    uint8_t ota_status = OTA_DONE;
    int ota_progress = 0, ota_content_length = 0, ota_total_read_len = 0;

    httpd_resp_set_type(req, "application/json");
    // Get URL
    if (httpd_req_get_url_query_str(req, param, sizeof(param)) == ESP_OK)
    {
        char action[16];
        int iaction = 0;
        // Get specific parameter
        if (httpd_query_key_value(param, "action", action, sizeof(action)) ==
            ESP_OK)
        {
            // Action by parameter
            iaction = atoi(action);
            syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_DEBUG,
                           "http fetch vue %d", iaction);
            http_printf(req, "{\"action-type\": %d,", iaction);
            switch (iaction)
            {
                case HTTP_LOADING_ID:
                    http_api_loading(req);
                    http_printf(req, "\"action-status\": %d}", HTTP_LOADING_ID);
                    break;
                case HTTP_AUTO_RESET_ID:
                    http_api_autolearn_clear(req);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_AUTO_DISPLAY_LOOP_ID:
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_AUTO_SAVE_ID:
                    http_api_autolearn_save(req);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_AUTO_LSTNS_ID:
                    if (http_api_autolearn_stillness(req) == ESP_OK)
                    {
                        http_printf(req, "\"action-status\": %d}",
                                    HTTP_ACTION_STATUS_SUCCESS);
                    }
                    else
                    {
                        http_printf(req, "\"action-status\": %d}",
                                    HTTP_ACTION_STATUS_FAIL);
                    }
                    break;
                case HTTP_AUTO_LSB_ID:
                    if (http_api_autolearn_somebody(req) == ESP_OK)
                    {
                        http_printf(req, "\"action-status\": %d}",
                                    HTTP_ACTION_STATUS_SUCCESS);
                    }
                    else
                    {
                        http_printf(req, "\"action-status\": %d}",
                                    HTTP_ACTION_STATUS_FAIL);
                    }
                    break;
                case HTTP_AUTO_LNB_ID:
                    if (http_api_autolearn_nobody(req) == ESP_OK)
                    {
                        http_printf(req, "\"action-status\": %d}",
                                    HTTP_ACTION_STATUS_SUCCESS);
                    }
                    else
                    {
                        http_printf(req, "\"action-status\": %d}",
                                    HTTP_ACTION_STATUS_FAIL);
                    }
                    break;
                case HTTP_AUTO_LSTOP_ID:
                    http_api_autolearn_stop(req);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_DEBUG_SOMEBODY_ID:
                    http_api_dbgsomebody(req);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_DEBUG_NOBODY_ID:
                    http_api_dbgnobody(req);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_DEBUG_OFF_ID:
                    http_api_dbgoff(req);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_OTA_ID:
                    nvs_handle_t nvs_handle;
                    ota_getstatus(&ota_status);
                    if (ota_status != OTA_IN_PROGRESS)
                    {
                        ret = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE,
                                       &nvs_handle);
                        if (ret == ESP_OK)
                        {
                            nvs_set_u8(nvs_handle, OTA_NVS_STATUS_KEY,
                                       ota_status);
                            nvs_close(nvs_handle);
                        }
                        system_task_creating(TASK_OTA_ID);
                        xTaskCreate(&task_ota, "task_ota", 8192, NULL, 5, NULL);
                        /* fall through */
                    }
                case HTTP_OTA_PROGRESS_ID:
                    ota_getstatus(&ota_status);
                    ota_getprogress(&ota_progress);
                    ota_getcontent_len(&ota_content_length);
                    ota_gettotal_readlen(&ota_total_read_len);
                    http_printf(req, "\"ota-total-get\": %d,",
                                ota_total_read_len);
                    http_printf(req, "\"ota-content-length\": %d,",
                                ota_content_length);
                    http_printf(req, "\"ota-percent\": %d,", ota_progress);
                    http_printf(req, "\"sysFirmwareupgradestatus\": %d,",
                                ota_status); /* Firmware Upgrade Status */
                    if (ota_progress == 100)
                    {
                        ota_msg = 1;
                        xQueueSend(gqueue_ota, &ota_msg, portMAX_DELAY);
                    }
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_OTA_ABORT_ID:
                    ota_abort();
                    ota_getstatus(&ota_status);
                    http_printf(req, "\"sysFirmwareupgradestatus\": %d,",
                                ota_status);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_REBOOT_ID:
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    http_api_reboot(req);
                    break;
                case HTTP_ERASEDATA_ID:
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    http_api_erasedata(req);
                    break;
                case HTTP_ENV_UPDT:
                    http_api_env_updt(req);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_RESET_BASELINE_ID:
                    http_api_reset_baseline(req);
                    http_printf(req, "\"action-status\": %d}",
                                HTTP_ACTION_STATUS_SUCCESS);
                    break;
                case HTTP_NU_DOWNLOAD_ID:
                {
                    /* Allocate buffers for Base64 encoded weights */
                    char *w_ih_b64 = malloc(4096);
                    char *w_ho_b64 = malloc(256);
                    char *b_h_b64 = malloc(256);
                    char *b_o_b64 = malloc(64);
                    /* Large buffer for full JSON content (no newlines) */
                    char *json_buf = malloc(8192);

                    if (w_ih_b64 && w_ho_b64 && b_h_b64 && b_o_b64 && json_buf)
                    {
                        if (nu_ld2410_get_weights_base64(w_ih_b64, w_ho_b64,
                                                         b_h_b64, b_o_b64))
                        {
                            /* Build JSON content without newlines */
                            int json_len =
                                snprintf(json_buf, 8192,
                                         "\"model\": \"" NU_MODEL_STR
                                         "\","
                                         "\"version\": 1,"
                                         "\"input_size\": %d,"
                                         "\"hidden_size\": %d,"
                                         "\"output_size\": %d,"
                                         "\"w_ih\": \"%s\","
                                         "\"w_ho\": \"%s\","
                                         "\"b_h\": \"%s\","
                                         "\"b_o\": \"%s\","
                                         "\"action-status\": %d}",
                                         INPUT_SIZE, HIDDEN_SIZE, OUTPUT_SIZE,
                                         w_ih_b64, w_ho_b64, b_h_b64, b_o_b64,
                                         HTTP_ACTION_STATUS_SUCCESS);
                            /* Send as single chunk (no newline added) */
                            httpd_resp_send_chunk(req, json_buf, json_len);
                        }
                        else
                        {
                            http_printf(req, "\"action-status\": %d}",
                                        HTTP_ACTION_STATUS_FAIL);
                        }
                    }
                    else
                    {
                        http_printf(req, "\"action-status\": %d}",
                                    HTTP_ACTION_STATUS_FAIL);
                    }

                    if (w_ih_b64) free(w_ih_b64);
                    if (w_ho_b64) free(w_ho_b64);
                    if (b_h_b64) free(b_h_b64);
                    if (b_o_b64) free(b_o_b64);
                    if (json_buf) free(json_buf);
                    break;
                }
                default:
                    httpd_resp_send_404(req);
                    return ESP_FAIL;
                    break;
            }
        }
        else
        {
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    http_printf_end(req);
    return ESP_OK;
}

static int handle_json_get_int(char *src, int *dst, char *obj)
{
    int i = 0, idstchar = 0;
    char temp[64] = {};
    char *strptr = NULL;
    if (src)
    {
        strptr = strstr(src, obj);
        if (strptr == NULL)
        {
            return false;
        }
        for (i = 0; i < 64; i++)
        {
            if (*(strptr + strlen(obj) + 2 + i) != '\"')
            {
                temp[idstchar] = *(strptr + strlen(obj) + 2 + i);
                idstchar++;
            }
            else
            {
                break;
            }
        }
    }
    if (idstchar)
    {
        *dst = atoi(temp);
        return true;
    }
    else
    {
        return false;
    }
}

static int handle_json_get_bool(char *src, int *dst, char *obj)
{
    char *strptr = NULL;
    if (src)
    {
        strptr = strstr(src, obj);
        if (strptr == NULL)
        {
            return false;
        }
        if (*(strptr + strlen(obj) + 1) == 't')
        {
            *dst = 1;
        }
        else
        {
            *dst = 0;
        }
    }
    return true;
}

static int handle_json_get_str(char *src, char *dst, int len, char *obj)
{
    char *strptr = NULL;

    if ((src == NULL) || (dst == NULL) || (len <= 0))
    {
        return false;
    }

    strptr = strstr(src, obj);
    if (strptr == NULL)
    {
        return false;
    }

    strptr += strlen(obj) + 2;

    while ((*strptr != '\0') && (*strptr != '\"') && (len > 1))
    {
        *dst = *strptr;
        dst++;
        strptr++;
        len--;
    }
    *dst = '\0';

    return true;
}

// Handler for POST requests to /submitform
esp_err_t handle_submitform(httpd_req_t *req)
{
    char *content = NULL;
    int ret, total_len = req->content_len, offset = 0;
    int idel_val = -1;
    char syslog_ip[16] = {};
    char firmware_ip[16] = {};
    char firmware_filename[33] = {};
    char apikey[THINGSPEAK_API_KEYLENGTH + 1] = {};
    int mq135high = 0;
    int mq135low = 0;
    int sgp41noxhigh = 0;
    int sgp41noxlow = 0;
    int temphigh = 0;
    int templow = 0;
    int humihigh = 0;
    int humilow = 0;
    int leddisplay = 0;
    int ledsnooze = 0;
    int facility = WEB_INPUT_INIT_VALUE;
    char facility_list_text[(SYSLOG_FACILITY_MAXNUM) * 4] = {0};
    int facility_ids[SYSLOG_FACILITY_MAXNUM] = {0};
    int facility_count = 0;
    int level = 0, leveltmp = 0;
    char levelstr[10] = {};
    int i = 0;
    int orghightemp = 0, orglowtemp = 0, orghighhumi = 0, orglowhumi = 0;
    int orgmq135thresholdhigh = 0, orgmq135thresholdlow = 0;
    int orgsgp41noxhigh = 0, orgsgp41noxlow = 0;
    char orgtpapikey[THINGSPEAK_API_KEYLENGTH + 1] = {0};
    uint8_t sys_mac[6];
    uint32_t delaytime = 0;
    int oridisplaytime = 0, oriledsnoozetime = 0;
    char ota_ip[OTA_MAXLEN_IP + 1], ota_filename[OTA_MAXLEN_FILENAME + 1],
        syslog_server_ip[SYSLOG_MAXLEN_IP + 1];
    uint32_t org_level = 0;

    memset(ota_ip, 0, sizeof(ota_ip));
    memset(ota_filename, 0, sizeof(ota_filename));
    memset(syslog_server_ip, 0, sizeof(syslog_server_ip));

    ota_getip(ota_ip, OTA_MAXLEN_IP);
    ota_getfilename(ota_filename, OTA_MAXLEN_FILENAME);
    syslog_get_server_ip(syslog_server_ip, SYSLOG_MAXLEN_IP);

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    // Allocate memory to hold the entire content
    content = malloc(total_len + 1);

    if (content == NULL)
    {
        return ESP_FAIL;  // Memory allocation failed
    }
    syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_DEBUG,
                   "handle submitform: %s", content);
    // Read the request body in chunks
    while (offset < total_len)
    {
        int size_to_read = MIN(total_len - offset, total_len - offset);
        ret = httpd_req_recv(req, content + offset, size_to_read);

        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;  // Retry if timeout
            }
            free(content);
            return ESP_FAIL;  // Read error
        }
        offset += ret;
    }
    // Null-terminate the received string
    content[total_len] = '\0';
#if !defined(LD2410_AUTOLEARN_NU)
    handle_json_get_int(content, &door_val, "\"door\"");
    handle_json_get_int(content, &act_val, "\"activeSensitivity\"");
    handle_json_get_int(content, &sta_val, "\"staticSensitivity\"");
#endif
    handle_json_get_int(content, &idel_val, "\"idelTimes\"");
    handle_json_get_str(content, syslog_ip, sizeof(syslog_ip) - 1,
                        "\"syslogIp\"");
    handle_json_get_str(content, firmware_ip, sizeof(firmware_ip) - 1,
                        "\"firmwareIp\"");
    handle_json_get_str(content, firmware_filename,
                        sizeof(firmware_filename) - 1, "\"firmwareFilename\"");
    handle_json_get_str(content, apikey, sizeof(apikey) - 1, "\"apikey\"");
    if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
    {
        handle_json_get_int(content, &mq135high, "\"mq135high\"");
        handle_json_get_int(content, &mq135low, "\"mq135low\"");
        handle_json_get_int(content, &sgp41noxhigh, "\"sgp41noxhigh\"");
        handle_json_get_int(content, &sgp41noxlow, "\"sgp41noxlow\"");
    }
    handle_json_get_int(content, &temphigh, "\"temphigh\"");
    handle_json_get_int(content, &templow, "\"templow\"");
    handle_json_get_int(content, &humihigh, "\"humihigh\"");
    handle_json_get_int(content, &humilow, "\"humilow\"");
    handle_json_get_int(content, &leddisplay, "\"leddisplay\"");
    handle_json_get_int(content, &ledsnooze, "\"ledsnooze\"");
    handle_json_get_int(content, &facility, "\"facility\"");
    handle_json_get_str(content, facility_list_text,
                        sizeof(facility_list_text) - 1, "\"facilityList\"");
    for (i = 0; i < SYSLOG_LEVEL_MAXNUM; i++)
    {
        snprintf(levelstr, sizeof(levelstr), "\"level%d\"", i);
        handle_json_get_bool(content, &leveltmp, levelstr);
        if (leveltmp)
        {
            level = level | 0x01 << i;
        }
        else
        {
            level = level & ~(0x01 << i);
        }
    }
    if (facility_list_text[0] != '\0')
    {
        char parse_buf[sizeof(facility_list_text)] = {0};
        char *saveptr = NULL;
        char *token = NULL;

        strncpy(parse_buf, facility_list_text, sizeof(parse_buf) - 1);
        token = strtok_r(parse_buf, ",", &saveptr);
        while (token != NULL && facility_count < SYSLOG_FACILITY_MAXNUM)
        {
            while (isspace((unsigned char)*token))
            {
                token++;
            }
            if (*token == '\0')
            {
                token = strtok_r(NULL, ",", &saveptr);
                continue;
            }
            char *end_trim = token + strlen(token) - 1;
            while (end_trim >= token && isspace((unsigned char)*end_trim))
            {
                *end_trim = '\0';
                end_trim--;
            }
            if (*token == '\0')
            {
                token = strtok_r(NULL, ",", &saveptr);
                continue;
            }
            char *endptr = NULL;
            long parsed = strtol(token, &endptr, 10);
            if (endptr == token || parsed < 0 ||
                parsed >= SYSLOG_FACILITY_MAXNUM)
            {
                token = strtok_r(NULL, ",", &saveptr);
                continue;
            }
            facility_ids[facility_count++] = (int)parsed;
            token = strtok_r(NULL, ",", &saveptr);
        }
        if (facility_count > 0)
        {
            facility = facility_ids[0];
        }
    }
    ld2410_getLeaveDelayTime(&delaytime);
    if (idel_val >= 0 && idel_val != delaytime)
    {
        ld2410_saveconfig(LD2410_NVS_LEARNSTATUS_KEY, idel_val);
        ld2410_setLeaveDelayTime((uint32_t)idel_val);
    }

    syslog_get_server_ip(syslog_server_ip, SYSLOG_MAXLEN_IP);
    bool ip_changed = (strcmp(syslog_ip, syslog_server_ip) != 0);
    bool level_changed = false;
    syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_DEBUG,
                   "OrgSyslogServer: %s, NewSyslogServer: %s", syslog_server_ip,
                   syslog_ip);
    if (ip_changed)
    {
        syslog_set_server_ip(syslog_ip, strlen(syslog_ip));
    }

    if (facility_count > 0)
    {
        for (i = 0; i < facility_count; i++)
        {
            int facility_id = facility_ids[i];
            syslog_get_facility_level(facility_id, &org_level);
            syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_DEBUG,
                           "Handle syslog facility %d, level %d", facility_id,
                           level);
            if (org_level != (uint32_t)level)
            {
                syslog_set_facility_level(facility_id, level);
                level_changed = true;
            }
        }
    }
    else
    {
        syslog_get_facility_level(facility, &org_level);
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_DEBUG,
                       "Handle syslog facility %d, level %d", facility, level);
        if ((facility != WEB_INPUT_INIT_VALUE) &&
            (org_level != (uint32_t)level))
        {
            syslog_set_facility_level(facility, level);
            level_changed = true;
        }
    }

    if (ip_changed || level_changed)
    {
        syslog_saveconfig(SYSLOG_NVS_SERVER_IP, syslog_server_ip);
    }

    if (strcmp(firmware_ip, ota_ip) || strcmp(firmware_filename, ota_filename))
    {
        ota_setip(firmware_ip, strlen(firmware_ip));
        ota_setfilename(firmware_filename, strlen(firmware_filename));
        ota_saveconfig(OTA_NVS_SERVER_IP, firmware_ip);
        ota_saveconfig(OTA_NVS_FILENAME, firmware_filename);
    }
    thingspeak_getapikey(orgtpapikey, THINGSPEAK_API_KEYLENGTH);
    if (strcmp(apikey, orgtpapikey))
    {
        thingspeak_setapikey(apikey, THINGSPEAK_API_KEYLENGTH);
        thingspeak_saveconfig();
    }

    if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
    {
        airquality_get_voc_threshold_high(&orgmq135thresholdhigh);
        airquality_get_voc_threshold_low(&orgmq135thresholdlow);
        if (mq135high != orgmq135thresholdhigh)
        {
            airquality_set_voc_threshold_high(mq135high);
        }
        if (mq135low != orgmq135thresholdlow)
        {
            airquality_set_voc_threshold_low(mq135low);
        }

        airquality_get_nox_threshold_high(&orgsgp41noxhigh);
        airquality_get_nox_threshold_low(&orgsgp41noxlow);
        if (sgp41noxhigh != orgsgp41noxhigh)
        {
            airquality_set_nox_threshold_high(sgp41noxhigh);
        }
        if (sgp41noxlow != orgsgp41noxlow)
        {
            airquality_set_nox_threshold_low(sgp41noxlow);
        }
    }

    dht22_gethightemperature(&orghightemp);
    dht22_getlowtemperature(&orglowtemp);
    dht22_gethighhumidity(&orghighhumi);
    dht22_getlowhumidity(&orglowhumi);

    if (temphigh != orghightemp)
    {
        dht22_sethightemperature(temphigh);
        dht22_saveconfig(DHT22_NVS_TEMP_THRESHOLD_HIGH_KEY, temphigh);
    }
    if (templow != orglowtemp)
    {
        dht22_setlowtemperature(templow);
        dht22_saveconfig(DHT22_NVS_TEMP_THRESHOLD_LOW_KEY, templow);
    }

    if (humihigh != orghighhumi)
    {
        dht22_sethighhumidity(humihigh);
        dht22_saveconfig(DHT22_NVS_HUMI_THRESHOLD_HIGH_KEY, humihigh);
    }
    if (humilow != orglowhumi)
    {
        dht22_setlowhumidity(humilow);
        dht22_saveconfig(DHT22_NVS_HUMI_THRESHOLD_LOW_KEY, humilow);
    }

    oled_getDisplayTime(&oridisplaytime);
    if (leddisplay != oridisplaytime)
    {
        oled_setDisplayTime(leddisplay);
        oled_saveconfig(OLED_NVS_DISPLAY_KEY, leddisplay);
    }
    oled_getSnoozeTime(&oriledsnoozetime);
    if (ledsnooze != oriledsnoozetime)
    {
        oled_setSnoozeTime(ledsnooze);
        oled_saveconfig(OLED_NVS_SNOOZE_KEY, ledsnooze);
    }

    free(content);
    // Send response
    const char resp[] = "Form data received and processed";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_api_autolearn_clear(httpd_req_t *req)
{
#if defined(LD2410_AUTOLEARN_NU)
    nu_ld2410_init_weights();
#endif
    return ESP_OK;
}

static esp_err_t http_api_autolearn_nobody(httpd_req_t *req)
{
    char ANType = 0;
    nu_ld2410_resetbuffer();
    ld2410_getANType(&ANType);
    if ((ANType & LD2410_AN_TYPE_SOMEONE) != LD2410_AN_TYPE_SOMEONE)
    {
        ld2410_setANType(LD2410_AN_TYPE_NOONE);
        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

static esp_err_t http_api_autolearn_save(httpd_req_t *req)
{
    if (gsemaLD2410 == NULL)
    {
        return ESP_FAIL;
    }
    if (xSemaphoreTake(gsemaLD2410, portMAX_DELAY) == pdTRUE)
    {
        nu_ld2410_saveweights();
        xSemaphoreGive(gsemaLD2410);
    }
    return ESP_OK;
}

static esp_err_t http_api_autolearn_stillness(httpd_req_t *req)
{
    char ANType = 0;
    nu_ld2410_resetbuffer();
    ld2410_getANType(&ANType);

    if ((ANType & LD2410_AN_TYPE_NOONE) != LD2410_AN_TYPE_NOONE)
    {
        ld2410_setANType(LD2410_AN_TYPE_STILLNESS);
        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

static esp_err_t http_api_autolearn_somebody(httpd_req_t *req)
{
    char ANType = 0;
    nu_ld2410_resetbuffer();
    ld2410_getANType(&ANType);
    syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_DEBUG, "get ANType %d",
                   ANType);
    if ((ANType & LD2410_AN_TYPE_NOONE) != LD2410_AN_TYPE_NOONE)
    {
        ld2410_setANType(LD2410_AN_TYPE_SOMEONE);
        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

static esp_err_t http_api_autolearn_stop(httpd_req_t *req)
{
    ld2410_setANType(LD2410_AN_TYPE_NONE);
    nu_ld2410_resetbuffer();
    return ESP_OK;
}

static esp_err_t http_api_dbgnobody(httpd_req_t *req)
{
    int flag = 0;
    ld2410_getDebuggingMode(&flag);
    ld2410_setDebuggingMode(flag | LD2410_DBG_FLAG_NOBODY);
    return ESP_OK;
}

static esp_err_t http_api_dbgoff(httpd_req_t *req)
{
    ld2410_setDebuggingMode(0);
    return ESP_OK;
}

static esp_err_t http_api_dbgsomebody(httpd_req_t *req)
{
    int flag = 0;
    ld2410_getDebuggingMode(&flag);
    ld2410_setDebuggingMode(flag | LD2410_DBG_FLAG_SOMEBODY);
    return ESP_OK;
}

static esp_err_t http_api_erasedata(httpd_req_t *req)
{
    system_seterasingnvs(true);
    // Erase homekit data and WiFi config (factory reset)
    // MUST be called before hap_stop() to ensure event is processed
    hap_reset_to_factory();

    // Give some time for HAP loop to process the reset event
    vTaskDelay(500 / portTICK_PERIOD_MS);

    hap_stop();

    // Erase NVS data
    nvs_flash_erase_partition("nvs");

    // Erase factory_nvs
    nvs_flash_erase_partition("factory_nvs");

    // Erase NVS encryption
    nvs_flash_erase_partition("nvs_keys");

    vTaskDelay(3000 / portTICK_PERIOD_MS);
    system_reboot();
    return ESP_OK;
}

static esp_err_t http_api_reset_baseline(httpd_req_t *req)
{
    airquality_reset_baseline();
    return ESP_OK;
}

static esp_err_t http_api_env_updt(httpd_req_t *req)
{
    int i = 0, temphigh = 0, templow = 0, humihigh = 0, humilow = 0;
    float temperature = 0, humidity = 0;
    int mq135aqi = 0, mq135thresholdhigh = 0, mq135thresholdlow = 0;
    int sgp41noxhigh = 0, sgp41noxlow = 0;
    uint8_t sys_mac[6], scheduler = 0;
    char ANType = 0;
    float pred = 0.0;

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
    {
        airquality_get_voc_index(&mq135aqi);
        int mq135nox = 0;
        airquality_get_nox_index(&mq135nox);
        http_printf(req, "\"sgp41nox\": %d,", mq135nox);
        airquality_get_voc_threshold_high(&mq135thresholdhigh);
        airquality_get_voc_threshold_low(&mq135thresholdlow);
        airquality_get_nox_threshold_high(&sgp41noxhigh);
        airquality_get_nox_threshold_low(&sgp41noxlow);
        http_printf(req, "\"mq135currentdata\": %d,",
                    mq135aqi); /* Current Air quality */
        http_printf(req, "\"mq135thresholdhigh\": %d,",
                    mq135thresholdhigh); /* Air quality worest threshold */
        http_printf(req, "\"mq135thresholdlow\": %d,",
                    mq135thresholdlow); /* Air quality best threshold */
        http_printf(req, "\"sgp41noxhigh\": %d,", sgp41noxhigh);
        http_printf(req, "\"sgp41noxlow\": %d,", sgp41noxlow);
        http_printf(req, "\"deltafanscheduler\": ["); /* DeltaFan Scheduler */
        for (i = 0; i < IR_DELTA_FAN_TIGGER_MODE_MAX - 1; i++)
        {
            ir_get_deltascheduler(i, &scheduler);
            http_printf(req, "%d,", scheduler);
        }
        ir_get_deltascheduler(i, &scheduler);
        http_printf(req, "%d],", scheduler);
    }
    dht22_getcurrenttemperature(&temperature);
    dht22_getcurrenthumidity(&humidity);
    dht22_getcurrenttemperature(&temperature);
    dht22_getcurrenthumidity(&humidity);
    dht22_gethightemperature(&temphigh);
    dht22_getlowtemperature(&templow);
    dht22_gethighhumidity(&humihigh);
    dht22_getlowhumidity(&humilow);
    http_printf(req, "\"dht22currenttemp\": %.1f,",
                temperature); /* Current Temperature */
    http_printf(req, "\"dht22thresholdtemphigh\": %d,",
                temphigh); /* High Temperature threshold */
    http_printf(req, "\"dht22thresholdtemplow\": %d,",
                templow); /* Low Temperature threshold */
    http_printf(req, "\"dht22currenthumi\": %.1f,",
                humidity); /* Current Humidity */
    http_printf(req, "\"dht22thresholdhumihigh\": %d,",
                humihigh); /* High Humidity threshold */
    http_printf(req, "\"dht22thresholdhumilow\": %d,",
                humilow); /* Low Humidity threshold */
#if defined(LD2410_AUTOLEARN_NU)
    nu_ld2410_getPred(&pred);
    http_printf(req, "\"nuld2410pred\": %f,",
                pred); /* Artificial Neural Network Prediction data */
    http_printf(req, "\"nuld2410new\": %d,",
                nu_ld2410_isnew()); /* ANN saved data is not latest */
#endif
    ld2410_getANType(&ANType);
    http_printf(req, "\"sysLearnstillnessstatus\": %d,",
                ANType & LD2410_AN_TYPE_STILLNESS); /* Learn Stillness Status */
    http_printf(req, "\"sysLearnsomebodystatus\": %d,",
                ANType & LD2410_AN_TYPE_SOMEONE); /* Learn Somebody Status */
    http_printf(req, "\"sysLearnnobodystatus\": %d,",
                ANType & LD2410_AN_TYPE_NOONE); /* Learn Nobody Statys */
    return ESP_OK;
}

static esp_err_t http_api_loading(httpd_req_t *req)
{
    int i = 0, temphigh = 0, templow = 0, humihigh = 0, humilow = 0;
    int mq135aqi = 0;
    float temperature = 0, humidity = 0;
    int mq135thresholdhigh = 0, mq135thresholdlow = 0;
    int sgp41noxhigh = 0, sgp41noxlow = 0;
    int flag = 0;
    uint8_t sys_mac[6], ota_status, scheduler = 0;
    uint32_t delaytime = 0;
    char ANType = 0;
    float pred = 0.0;
    int leddisplaytime = 0, ledsnoozetime = 0;
    uint32_t level = 0;
    char syslog_server_ip[SYSLOG_MAXLEN_IP + 1] = {0};
    char ota_ip[OTA_MAXLEN_IP + 1] = {0};
    char ota_filename[OTA_MAXLEN_FILENAME + 1] = {0};
    char thingspeak_apikey[THINGSPEAK_API_KEYLENGTH + 1] = {0};
    esp_netif_ip_info_t sys_ip_info;

    ota_getstatus(&ota_status);
    system_get_ip(&sys_ip_info);
    syslog_get_server_ip(syslog_server_ip, sizeof(syslog_server_ip) - 1);
    ota_getip(ota_ip, sizeof(ota_ip) - 1);
    ota_getfilename(ota_filename, sizeof(ota_filename) - 1);
    thingspeak_getapikey(thingspeak_apikey, sizeof(thingspeak_apikey) - 1);

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    http_printf(req, "\"sysBuildversion\": %s,",
                TOSTRING(BUILD_VERSION)); /* Build version */
    http_printf(req, "\"sysBuildTime\": \"%s %s\",", __DATE__,
                __TIME__); /* Build version */

    ld2410_getDebuggingMode(&flag);
    ld2410_getANType(&ANType);
    http_printf(req, "\"sysMacaddress\": \"%02X:%02X:%02X:%02X:%02X:%02X\",",
                sys_mac[0], sys_mac[1], sys_mac[2], sys_mac[3], sys_mac[4],
                sys_mac[5]); /* MACaddress */
    http_printf(req, "\"sysIPaddress\": \"" IPSTR "\",",
                IP2STR(&sys_ip_info.ip)); /* IPaddress */
    ld2410_getLeaveDelayTime(&delaytime);
    http_printf(req, "\"sysOffthreshold\": %d,", delaytime); /* Off threshold */
    http_printf(req, "\"sysLearnstillnessstatus\": %d,",
                ANType & LD2410_AN_TYPE_STILLNESS); /* Learn Stillness Status */
    http_printf(req, "\"sysLearnsomebodystatus\": %d,",
                ANType & LD2410_AN_TYPE_SOMEONE); /* Learn Somebody Status */
    http_printf(req, "\"sysLearnnobodystatus\": %d,",
                ANType & LD2410_AN_TYPE_NOONE); /* Learn Nobody Statys */
    http_printf(req, "\"sysDebugsomebodystatus\": %d,",
                flag & LD2410_DBG_FLAG_SOMEBODY); /* Debug Somebody Status */
    http_printf(req, "\"sysDebugnobodystatus\": %d,",
                flag & LD2410_DBG_FLAG_NOBODY); /* Debug Nobody Status */
    http_printf(req, "\"sysFirmwareupgradestatus\": %d,",
                ota_status); /* Firmware Upgrade Status */
    http_printf(req, "\"syslogIp\": \"%s\",",
                syslog_server_ip); /* Syslog server */
    http_printf(req, "\"firmwareIp\": \"%s\",",
                ota_ip); /* Firmware server IP */
    http_printf(req, "\"firmwareFilename\": \"%s\",",
                ota_filename); /* Firmware filename */
    http_printf(req, "\"apikey\": \"%s\",",
                thingspeak_apikey); /* ThingSpeak API key */
    http_printf(req, "\"sysRebootstatus\": %d,",
                system_isrebooting()); /* Reboot Status */
    http_printf(req, "\"sysErasestatus\": %d,",
                system_iserasingnvs()); /* Erase Date Status */
    if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
    {
        airquality_get_voc_index(&mq135aqi);
        airquality_get_voc_threshold_high(&mq135thresholdhigh);
        airquality_get_voc_threshold_low(&mq135thresholdlow);
        airquality_get_nox_threshold_high(&sgp41noxhigh);
        airquality_get_nox_threshold_low(&sgp41noxlow);
        http_printf(req, "\"mq135currentdata\": %u,",
                    mq135aqi); /* Current Air quality */
        http_printf(req, "\"mq135thresholdhigh\": %d,",
                    mq135thresholdhigh); /* Air quality worest threshold */
        http_printf(req, "\"mq135thresholdlow\": %d,",
                    mq135thresholdlow); /* Air quality best threshold */
        http_printf(req, "\"sgp41noxhigh\": %d,", sgp41noxhigh);
        http_printf(req, "\"sgp41noxlow\": %d,", sgp41noxlow);
        http_printf(req, "\"deltafanscheduler\": ["); /* DeltaFan Scheduler */
        for (i = 0; i < IR_DELTA_FAN_TIGGER_MODE_MAX - 1; i++)
        {
            ir_get_deltascheduler(i, &scheduler);
            http_printf(req, "%d,", scheduler);
        }
        ir_get_deltascheduler(i, &scheduler);
        http_printf(req, "%d],", scheduler);
    }
    dht22_getcurrenttemperature(&temperature);
    dht22_getcurrenthumidity(&humidity);
    dht22_gethightemperature(&temphigh);
    dht22_getlowtemperature(&templow);
    dht22_gethighhumidity(&humihigh);
    dht22_getlowhumidity(&humilow);
    http_printf(req, "\"dht22currenttemp\": %.1f,",
                temperature); /* Current Temperature */
    http_printf(req, "\"dht22thresholdtemphigh\": %d,",
                temphigh); /* High Temperature threshold */
    http_printf(req, "\"dht22thresholdtemplow\": %d,",
                templow); /* Low Temperature threshold */
    http_printf(req, "\"dht22currenthumi\": %.1f,",
                humidity); /* Current Humidity */
    http_printf(req, "\"dht22thresholdhumihigh\": %d,",
                humihigh); /* High Humidity threshold */
    http_printf(req, "\"dht22thresholdhumilow\": %d,",
                humilow); /* Low Humidity threshold */
#if defined(LD2410_AUTOLEARN_NU)
    nu_ld2410_getPred(&pred);
    http_printf(req, "\"nuld2410pred\": %f,",
                pred); /* Artificial Neural Network Prediction data */
    http_printf(req, "\"nuld2410new\": %d,",
                nu_ld2410_isnew()); /* ANN saved data is not latest */
#endif
    oled_getDisplayTime(&leddisplaytime);
    oled_getSnoozeTime(&ledsnoozetime);
    http_printf(req, "\"leddisplay\": %d,",
                leddisplaytime);                           /* LED Sleep time */
    http_printf(req, "\"ledsnooze\": %d,", ledsnoozetime); /* LED Snooze time */
    http_printf(req, "\"syslogstatus\": [");               /* syslogstatus */
    for (i = 0; i < SYSLOG_FACILITY_MAXNUM - 1; i++)
    {
        syslog_get_facility_level(i, &level);
        http_printf(req, "%d,", level);
    }
    syslog_get_facility_level(i, &level);
    http_printf(req, "%d],", level);
    return ESP_OK;
}

static esp_err_t http_api_reboot(httpd_req_t *req)
{
    system_reboot();
    return ESP_OK;
}

esp_err_t http_homevue(httpd_req_t *req)
{
    const char *file_path = "/spiffs/index.html";
    FILE *f = fopen(file_path, "r");
    if (f == NULL)
    {
        ESP_LOGE("webpages", "Failed to open %s", file_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to open UI");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");

    char buffer[256];
    size_t read_bytes = 0;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK)
        {
            fclose(f);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static int http_printf_end(httpd_req_t *req)
{
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static int http_printf(httpd_req_t *req, const char *fmt, ...)
{
    char buf[256] = {};
    char outbuf[259] = {};
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sprintf(outbuf, "%s\n", buf);
    if (len > 0)
    {
        httpd_resp_send_chunk(req, outbuf, strlen(outbuf));
    }
    return len;
}

/* Handler for POST requests to /nu_upload - Upload NU weights to memory */
static esp_err_t handle_nu_upload(httpd_req_t *req)
{
    char *content = NULL;
    int total_len = req->content_len;
    int offset = 0;
    int ret;
    char *w_ih_b64 = NULL;
    char *w_ho_b64 = NULL;
    char *b_h_b64 = NULL;
    char *b_o_b64 = NULL;
    bool success = false;

    httpd_resp_set_type(req, "application/json");

    /* Allocate memory for content */
    content = malloc(total_len + 1);
    if (content == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "{\"error\": \"Memory allocation failed\"}");
        return ESP_FAIL;
    }

    /* Read the request body */
    while (offset < total_len)
    {
        ret = httpd_req_recv(req, content + offset, total_len - offset);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            free(content);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "{\"error\": \"Failed to receive data\"}");
            return ESP_FAIL;
        }
        offset += ret;
    }
    content[total_len] = '\0';

    syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_DEBUG,
                   "NU upload received %d bytes", total_len);

    /* Allocate buffers for Base64 strings */
    w_ih_b64 = malloc(4096);
    w_ho_b64 = malloc(256);
    b_h_b64 = malloc(256);
    b_o_b64 = malloc(64);

    if (!w_ih_b64 || !w_ho_b64 || !b_h_b64 || !b_o_b64)
    {
        goto cleanup;
    }

    /* Extract Base64 strings from JSON */
    if (!handle_json_get_str(content, w_ih_b64, 4095, "\"w_ih\""))
    {
        syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_ERROR,
                       "Failed to parse w_ih from JSON");
        goto cleanup;
    }
    if (!handle_json_get_str(content, w_ho_b64, 255, "\"w_ho\""))
    {
        syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_ERROR,
                       "Failed to parse w_ho from JSON");
        goto cleanup;
    }
    if (!handle_json_get_str(content, b_h_b64, 255, "\"b_h\""))
    {
        syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_ERROR,
                       "Failed to parse b_h from JSON");
        goto cleanup;
    }
    if (!handle_json_get_str(content, b_o_b64, 63, "\"b_o\""))
    {
        syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_ERROR,
                       "Failed to parse b_o from JSON");
        goto cleanup;
    }

    /* Set weights from Base64 (loads to memory only, not saved to SPIFFS) */
    if (nu_ld2410_set_weights_from_base64(w_ih_b64, w_ho_b64, b_h_b64, b_o_b64))
    {
        success = true;
        syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_INFO,
                       "NU weights uploaded to memory successfully");
    }
    else
    {
        syslog_handler(SYSLOG_FACILITY_WEB, SYSLOG_LEVEL_ERROR,
                       "Failed to set weights from Base64");
    }

cleanup:
    if (content) free(content);
    if (w_ih_b64) free(w_ih_b64);
    if (w_ho_b64) free(w_ho_b64);
    if (b_h_b64) free(b_h_b64);
    if (b_o_b64) free(b_o_b64);

    if (success)
    {
        httpd_resp_sendstr(req,
                           "{\"status\": \"success\", \"message\": \"Weights "
                           "loaded to memory. Press Save to persist.\"}");
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "{\"status\": \"error\", \"message\": \"Failed to "
                            "parse or apply weights\"}");
    }

    return success ? ESP_OK : ESP_FAIL;
}

// Setup HTTP service
httpd_handle_t http_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;  // Using port 8080
    config.max_uri_handlers = 12;
    config.max_resp_headers = 10;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t homevue_uri = {.uri = "/vue",
                                   .method = HTTP_GET,
                                   .handler = http_homevue,
                                   .user_ctx = NULL};
        httpd_uri_t fetch_vue_uri = {.uri = "/fetchvue",
                                     .method = HTTP_GET,
                                     .handler = fetch_vue,
                                     .user_ctx = NULL};
        // URI handler for /submitform (for handling POST requests)
        httpd_uri_t submitform_uri = {.uri = "/submitform",
                                      .method = HTTP_POST,
                                      .handler = handle_submitform,
                                      .user_ctx = NULL};
        // URI handler for /nu_upload (for uploading NU weights)
        httpd_uri_t nu_upload_uri = {.uri = "/nu_upload",
                                     .method = HTTP_POST,
                                     .handler = handle_nu_upload,
                                     .user_ctx = NULL};
        httpd_register_uri_handler(server, &homevue_uri);
        httpd_register_uri_handler(server, &fetch_vue_uri);
        httpd_register_uri_handler(server, &submitform_uri);
        httpd_register_uri_handler(server, &nu_upload_uri);
    }
    printf("\n HTTP task init down.\n");
    return server;
}

#if 0
static void http_view_css(httpd_req_t *req)
{    
    /* CSS */    
    http_printf(req, "<style>");
    http_printf(req, "#OTAprogress-bar {");
    http_printf(req, "  width: 100%%;");
    http_printf(req, "  background-color: #f3f3f3;");
    http_printf(req, "  border: 1px solid #ccc;");
    http_printf(req, "}");
    http_printf(req, "#OTAprogress {");
    http_printf(req, "  width: 0;");
    http_printf(req, "  height: 30px;");
    http_printf(req, "  background-color: #4caf50;");
    http_printf(req, "  text-align: center;");
    http_printf(req, "  line-height: 30px;");
    http_printf(req, "  color: white;");
    http_printf(req, "}");
    http_printf(req, ".hidden {"); 
    http_printf(req, "  display: none;");
    http_printf(req, "}");
    http_printf(req, "button:disabled {");
    http_printf(req, "  background-color: #ccc;");
    http_printf(req, "  cursor: not-allowed;");
    http_printf(req, "}");
    http_printf(req, ".Counterbar-container {");
    http_printf(req, "  width: 100%;");
    http_printf(req, "  background-color: #f3f3f3;");
    http_printf(req, "  border: 1px solid #ccc;");
    http_printf(req, "  position: relative;");
    http_printf(req, "  height: 30px;");
    http_printf(req, "}");
    http_printf(req, ".Counterbar {");
    http_printf(req, "  height: 100%;");
    http_printf(req, "  background-color: #4caf50;");
    http_printf(req, "  text-align: center;");
    http_printf(req, "  color: black;");
    http_printf(req, "  line-height: 30px;");
    http_printf(req, "}");
    /* New styles for table */
    http_printf(req, "table {");
    http_printf(req, "width: 100%;");
    http_printf(req, "border-collapse: collapse;");
    http_printf(req, "margin-bottom: 20px;");
    http_printf(req, "}");
    http_printf(req, "th, td {");
    http_printf(req, "border: 1px solid #ddd;");
    http_printf(req, "padding: 8px;");
    http_printf(req, "}");
    http_printf(req, "th {");
    http_printf(req, "background-color: #4caf50;");
    http_printf(req, "color: white;");
    http_printf(req, "}");
    http_printf(req, "tr:nth-child(even) {");
    http_printf(req, "background-color: #f2f2f2;");
    http_printf(req, "}");
    http_printf(req, "tr:hover {");
    http_printf(req, "background-color: #ddd;");
    http_printf(req, "}");
    /* New styles for div elements */
    http_printf(req, "#title, #test {");
    http_printf(req, "font-weight: bold;");
    http_printf(req, "}");
    http_printf(req, "#title {");
    http_printf(req, "background-color: #4caf50;");
    http_printf(req, "color: white;");
    http_printf(req, "padding: 5px;");
    http_printf(req, "text-align: center;");
    http_printf(req, "}");
    http_printf(req, "#test {");
    http_printf(req, "background-color: #2196F3;");
    http_printf(req, "color: white;");
    http_printf(req, "padding: 5px;");
    http_printf(req, "text-align: center;");
    http_printf(req, "}");
    /* Section background colors */
    http_printf(req, ".section {");
    http_printf(req, "background-color: #e0f7fa;"); /* Light cyan background for sections */
    http_printf(req, "margin-bottom: 20px;");
    http_printf(req, "padding: 10px;");
    http_printf(req, "border-radius: 5px;");
    http_printf(req, "}");
    http_printf(req, ".section-result {");
    http_printf(req, "background-color: #ffe0b2;"); /* Light orange background for results */
    http_printf(req, "margin-bottom: 20px;");
    http_printf(req, "padding: 10px;");
    http_printf(req, "border-radius: 5px;");
    http_printf(req, "}");
    http_printf(req, "</style>");
    http_printf(req, "</head>");
}   

static void http_view_display_input_table(httpd_req_t *req)
{
    int i = 0;
    uint8_t sys_mac[6];

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    http_printf(req, "<div class=\"section\" v-show=\"showConfig\">");
    http_printf(req, "<form @submit.prevent=\"submitForm\">");

#if !defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "<label for=\"door\">Door:</label>");
    http_printf(req, "<input type=\"number\" v-model=\"form.door\" id=\"door\"><br><br>");
    
    http_printf(req, "<label for=\"activeSensitivity\">Active Sensitivity:</label>");
    http_printf(req, "<input type=\"number\" v-model=\"form.activeSensitivity\" id=\"activeSensitivity\"><br><br>");
    
    http_printf(req, "<label for=\"staticSensitivity\">Static Sensitivity:</label>");
    http_printf(req, "<input type=\"number\" v-model=\"form.staticSensitivity\" id=\"staticSensitivity\"><br><br>");
#endif
    http_printf(req, "<label for=\"idelTimes\">Idel Times:</label>");
    http_printf(req, "<input type=\"number\" v-model=\"form.idelTimes\" id=\"idelTimes\"><br><br>");

    http_printf(req, "<label for=\"syslogIp\">Syslog IP:</label>");
    http_printf(req, "<input type=\"text\" v-model=\"form.syslogIp\" id=\"syslogIp\"><br><br>");
    
    http_printf(req, "<label for=\"firmwareIp\">Firmware Upgrade Url: https://</label>");
    http_printf(req, "<input type=\"text\" v-model=\"form.firmwareIp\" id=\"firmwareIp\">:8443/");
    http_printf(req, "<input type=\"text\" v-model=\"form.firmwareFilename\" id=\"firmwareFilename\"><br><br>");
    
    http_printf(req, "<label for=\"ThingSpeak\">ThingSpeak APIKEY</label>");
    http_printf(req, "<input type=\"text\" v-model=\"form.apikey\" id=\"apikey\"><br><br>");
    
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        http_printf(req, "<label for=\"mq135\">Air Quality Threshold:<br> High</label>");
        http_printf(req, "<input type=\"number\" v-model=\"form.mq135high\" id=\"mq135high\"> <br> Low");
        http_printf(req, "<input type=\"number\" v-model=\"form.mq135low\" id=\"mq135low\"> <br><br>");
    }

    http_printf(req, "<label for=\"dht22temp\">Temperature Threshold:<br> High</label>");
    http_printf(req, "<input type=\"number\" v-model=\"form.temphigh\" id=\"temphigh\"> (°C)<br> Low");
    http_printf(req, "<input type=\"number\" v-model=\"form.templow\" id=\"templow\"> (°C)<br><br>");

    http_printf(req, "<label for=\"dht22humi\">Humidity Threshold:<br> High</label>");
    http_printf(req, "<input type=\"number\" v-model=\"form.humihigh\" id=\"humihigh\"> (%%)<br> Low");
    http_printf(req, "<input type=\"number\" v-model=\"form.humilow\" id=\"humilow\"> (%%)<br><br>");

    http_printf(req, "<label for=\"OLED\">LED ECO:<br> Display Time</label>");
    http_printf(req, "<input type=\"number\" v-model=\"form.leddisplay\" id=\"leddisplay\"> (sec.)<br> Snooze Time");
    http_printf(req, "<input type=\"number\" v-model=\"form.ledsnooze\" id=\"ledsnooze\"> (sec.)<br><br>");

    http_printf(req, "<label for=\"RLOG\">Remote Log Status:</label>");
    http_printf(req, "<table>");
    http_printf(req, "<tr><td>Function</td>");
    for(i=0;i<SYSLOG_LEVEL_MAXNUM;i++)
    {
        http_printf(req, "<td>%s</td>",gsyslog_level_str[i]);
    }
    http_printf(req, "</tr>");
    http_printf(req, "<tr><td>");
    http_printf(req, "<select id=\"facility\" multiple v-model=\"form.facilities\" @change=\"updateLevelsByFacility\">");
    for(i=0;i<SYSLOG_FACILITY_MAXNUM;i++)
    {
        http_printf(req, "<option value=%d>%s</option>",i,gsyslog_facility_str[i]);
    }
    http_printf(req, "</select>");
    http_printf(req, "</td>");
    for(i=0;i<SYSLOG_LEVEL_MAXNUM;i++)
    {
        http_printf(req, "<td><input type=\"checkbox\" id=\"level%d\" v-model=\"form.level%d\" value=\"1\" @change=\"updateLocalLogLevel\"></td>",i,i);
    }
    http_printf(req, "</table>");

    http_printf(req, "<button type=\"submit\">Submit</button>");
    http_printf(req, "</form>");
    http_printf(req, "</div>");
}

static void http_view_firmware_update_pregress_table(httpd_req_t *req)
{
    /* Firmware Upgrade Progress Table */
    http_printf(req, "<div class=\"section-result\" v-show=\"showOTAProgressBar\">");
    http_printf(req, "<table width=\"300\" border=\"0\">");
    http_printf(req, "<tbody>");
    http_printf(req, "  <tr>");
    http_printf(req, "    <td>Firmware Upgrade Progress</td>");
    http_printf(req, "  </tr>");
    http_printf(req, "  <tr>");
    http_printf(req, "    <td>");
    http_printf(req, "      <div id=\"OTAprogress-bar\">");
    http_printf(req, "        <div id=\"OTAprogress\" :style=\"{ width: otaPercent + '%%',backgroundColor: getColorForPercentage(otaPercent)}\">{{ otaPercent >= 100 ? 'Rebooting, Please Wait (' + otaCountdown + ' sec.)' : otaPercentText }}</div>");
    http_printf(req, "      </div>");
    http_printf(req, "    </td>");
    http_printf(req, "  </tr>");
    http_printf(req, "</tbody>");
    http_printf(req, "</table>");
    http_printf(req, "</div>");
}

static void http_view_method(httpd_req_t *req)
{
    uint8_t sys_mac[6];

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    /* Method */
    http_printf(req, "  methods: {");
    http_printf(req, "    fetchData(action) {");
    http_printf(req, "      console.log(`Fetching data for action ${action}`);"); // Add debug log
    http_printf(req, "      fetch(`/fetchvue?action=${action}`)");
    http_printf(req, "      .then(response => response.json())");
    http_printf(req, "      .then(data => {");
//http_printf(req, "        console.log('Data received:', data);"); // Add debug log
    http_printf(req, "        if (data.error) {");
    http_printf(req, "          alert(data.error);");
    http_printf(req, "        } else {");
    http_printf(req, "          const action_type = data['action-type'];");
    http_printf(req, "          const action_status = data['action-status'];");
    http_printf(req, "          switch(action_type){");
    http_printf(req, "            case %d:",HTTP_LOADING_ID);
    http_printf(req, "              this.sysBuildversion = data.sysBuildversion || 'Unknown';");
    http_printf(req, "              this.sysBuildTime = data.sysBuildTime || 'Unknown';");
    http_printf(req, "              this.sysMacaddress = data.sysMacaddress || 'Unknown';");
    http_printf(req, "              this.sysIPaddress = data.sysIPaddress || 'Unknown';");
    http_printf(req, "              this.sysOffthreshold = data.sysOffthreshold || 0;");
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        http_printf(req, "              this.mq135currentdata = data.mq135currentdata || 0;");
        http_printf(req, "              this.mq135thresholdhigh = data.mq135thresholdhigh || 0;");
        http_printf(req, "              this.mq135thresholdlow = data.mq135thresholdlow || 0;");
        http_printf(req, "              this.deltafanscheduler = data.deltafanscheduler || 0;");
    }
    http_printf(req, "              this.dht22currenttemp = data.dht22currenttemp || 0;");
    http_printf(req, "              this.dht22thresholdtemphigh = data.dht22thresholdtemphigh || 0;");
    http_printf(req, "              this.dht22thresholdtemplow = data.dht22thresholdtemplow || 0;");
    http_printf(req, "              this.dht22currenthumi = data.dht22currenthumi || 0;");
    http_printf(req, "              this.dht22thresholdhumihigh = data.dht22thresholdhumihigh || 0;");
    http_printf(req, "              this.dht22thresholdhumilow = data.dht22thresholdhumilow || 0;");
#if defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "              this.nuld2410pred = data.nuld2410pred || 0;");
    http_printf(req, "              this.nuld2410new = data.nuld2410new || 0;");
#endif
    http_printf(req, "              this.leddisplay = data.leddisplay || 0;");
    http_printf(req, "              this.ledsnooze = data.ledsnooze || 0;");
    http_printf(req, "              this.syslogstatus = Array.isArray(data.syslogstatus) ? data.syslogstatus : [];");
    http_printf(req, "              this.sysResetstatus = data.sysResetstatus || 0;");
    http_printf(req, "              this.sysDisplaySsatus = data.sysDisplaySsatus || 0;");
    http_printf(req, "              this.sysSavestatus = data.sysSavestatus || 0;");
    http_printf(req, "              this.sysLearnstillnessstatus = data.sysLearnstillnessstatus || 0;");
    http_printf(req, "              this.sysLearnsomebodystatus = data.sysLearnsomebodystatus || 0;");
    http_printf(req, "              this.sysLearnnobodystatus = data.sysLearnnobodystatus || 0;");
    http_printf(req, "              this.sysDebugsomebodystatus = data.sysDebugsomebodystatus || 0;");
    http_printf(req, "              this.sysDebugnobodystatus = data.sysDebugnobodystatus || 0;");
    http_printf(req, "              this.sysFirmwareupgradestatus = data.sysFirmwareupgradestatus || 0;");
    http_printf(req, "              this.sysRebootstatus = data.sysRebootstatus || 0;");
    http_printf(req, "              this.sysErasestatus = data.sysErasestatus || 0;");
    http_printf(req, "              if (this.displayInterval)");
    http_printf(req, "              {");
#if defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "                  this.bottonTextDisplay = \"Syslog Input Data\";");
#else     
    http_printf(req, "                  this.bottonTextDisplay = \"Hide Result\";");
#endif    
    http_printf(req, "              }");
    http_printf(req, "              else");
    http_printf(req, "              {");
#if defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "                  this.bottonTextDisplay = \"Syslog Input Data\";");
#else     
    http_printf(req, "                  this.bottonTextDisplay = \"Display Result\";");
#endif
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysSavestatus)");
    http_printf(req, "              {");
    http_printf(req, "                  ;");
    http_printf(req, "              }");
#if defined(LD2410_AUTOLEARN_NU)    
    http_printf(req, "              if(this.nuld2410new)");
    http_printf(req, "              {");
    http_printf(req, "                  this.isSaveDisabled = false;");    
    http_printf(req, "              }");
    http_printf(req, "              else");
    http_printf(req, "              {");
    http_printf(req, "                  this.isSaveDisabled = true;");
    http_printf(req, "              }");
#endif    
    http_printf(req, "              if(this.sysLearnstillnessstatus)");
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showOTAProgressBar = false;");
    http_printf(req, "                  this.showStillButton = true;");
    http_printf(req, "                  this.showSoBodyButton = false;");    
    http_printf(req, "                  this.showNoBodyButton = false;");    
    http_printf(req, "                  this.showStopButton = true;");    
    http_printf(req, "                  this.isLearnStillDisabled = true;");
    http_printf(req, "                  this.isStopDisabled = false;"); 
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysLearnsomebodystatus)");
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showOTAProgressBar = false;");
    http_printf(req, "                  this.showStillButton = false;");
    http_printf(req, "                  this.showSoBodyButton = true;");    
    http_printf(req, "                  this.showNoBodyButton = false;");
    http_printf(req, "                  this.showStopButton = true;");
    http_printf(req, "                  this.isLearnSoBodyDisabled = true;");
    http_printf(req, "                  this.isStopDisabled = false;"); 
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysLearnnobodystatus)");
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showOTAProgressBar = false;");
    http_printf(req, "                  this.showStillButton = false;");    
    http_printf(req, "                  this.showSoBodyButton = false;");
    http_printf(req, "                  this.showNoBodyButton = true;");
    http_printf(req, "                  this.showStopButton = true;");
    http_printf(req, "                  this.isLearnNoBodyDisabled = true;");
    http_printf(req, "                  this.isStopDisabled = false;"); 
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysDebugsomebodystatus)");
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showOTAProgressBar = false;");
    http_printf(req, "                  this.isDebugSomebodyDisabled = true;");
    http_printf(req, "                  this.isDebugOffDisabled = false;");
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysDebugnobodystatus)");
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showOTAProgressBar = false;");
    http_printf(req, "                  this.isDebugNobodyDisabled = true;");
    http_printf(req, "                  this.isDebugOffDisabled = false;");
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysFirmwareupgradestatus==%d)",OTA_IN_PROGRESS);
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showConfig = false;");
    http_printf(req, "                  this.showOTAProgressBar = true;");
    http_printf(req, "                  this.isResetDisabled = true;");
    http_printf(req, "                  this.isDisplayDisabled = true;");
    http_printf(req, "                  this.isSaveDisabled = true;");
    http_printf(req, "                  this.isLearnStillDisabled = true;");
    http_printf(req, "                  this.isLearnSoBodyDisabled = true;");
    http_printf(req, "                  this.isLearnNoBodyDisabled = true;");
    http_printf(req, "                  this.isStopDisabled = true;");
    http_printf(req, "                  this.isDebugSomebodyDisabled = true;");
    http_printf(req, "                  this.isDebugNobodyDisabled = true;");    
    http_printf(req, "                  this.isDebugOffDisabled = true;");
    http_printf(req, "                  this.isFirmwareUpgradeDisabled = true;");
    http_printf(req, "                  this.isRebootDisabled = true;");
    http_printf(req, "                  this.isEraseDataDisabled = true;");
    http_printf(req, "                  this.startFirmwareUpgrade();");
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysRebootstatus)");
    http_printf(req, "              {");
    http_printf(req, "                   if (this.displayInterval) {");
    http_printf(req, "                     clearInterval(this.displayInterval);");
    http_printf(req, "                   }");
    http_printf(req, "                   this.showAutoLearning = false;");    
    http_printf(req, "                   this.showConfig = false;");
    http_printf(req, "                   this.showOTAProgressBar = true;");
    http_printf(req, "                   this.otaPercent = 100;");
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysErasestatus)");
    http_printf(req, "              {");
    http_printf(req, "                  ;");
    http_printf(req, "              }");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_AUTO_RESET_ID);
    http_printf(req, "              this.showAutoLearning = false;");    
    http_printf(req, "              this.showOTAProgressBar = false;");    
    http_printf(req, "              this.showConfig = true;");
    http_printf(req, "              break;");
    http_printf(req, "              this.showOTAProgressBar = false;");
    http_printf(req, "              this.isSaveDisabled = false;");
    http_printf(req, "              this.startDisplayAutoLearn();");    
    http_printf(req, "            case %d:",HTTP_AUTO_DISPLAY_LOOP_ID);
#if !defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "              this.auto_learn_maxdis = data['auto-learn-maxdis']||0;");
    http_printf(req, "              this.auto_learn_nbtal = data['auto-learn-nbtal']||0;");
    http_printf(req, "              this.auto_learn_sbtal = data['auto-learn-sbtal']||0;");
    http_printf(req, "              this.auto_learn_result = Array.from(data[`auto-learn-result`]);");
    http_printf(req, "              this.auto_learn_counter = Array.from(data[`auto-learn-counter`]);");
#endif
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_AUTO_SAVE_ID);
    http_printf(req, "              this.showStillButton = true;");
    http_printf(req, "              this.showNoBodyButton = true;");
    http_printf(req, "              this.showSoBodyButton = true;");
    http_printf(req, "              this.showStopButton = false;");
    http_printf(req, "              this.isLearnStillDisabled = false;");    
    http_printf(req, "              this.isLearnSoBodyDisabled = false;");
    http_printf(req, "              this.isLearnNoBodyDisabled = false;");
    http_printf(req, "              this.isStopDisabled = true;");     
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_AUTO_LSB_ID);
    http_printf(req, "              this.showStillButton = false;");
    http_printf(req, "              this.showSoBodyButton = true;");    
    http_printf(req, "              this.showNoBodyButton = false;");
    http_printf(req, "              this.showStopButton = true;");
    http_printf(req, "              this.isLearnSoBodyDisabled = true;");
    http_printf(req, "              this.isStopDisabled = false;");     
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_AUTO_LNB_ID);
    http_printf(req, "              this.showStillButton = false;");    
    http_printf(req, "              this.showSoBodyButton = false;");
    http_printf(req, "              this.showNoBodyButton = true;");    
    http_printf(req, "              this.showStopButton = true;");
    http_printf(req, "              this.isLearnNoBodyDisabled = true;");
    http_printf(req, "              this.isStopDisabled = false;");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_AUTO_LSTNS_ID);
    http_printf(req, "              this.showStillButton = true;");
    http_printf(req, "              this.showSoBodyButton = false;");    
    http_printf(req, "              this.showNoBodyButton = false;");
    http_printf(req, "              this.showStopButton = true;");    
    http_printf(req, "              this.isLearnStillDisabled = true;");
    http_printf(req, "              this.isStopDisabled = false;");     
    http_printf(req, "              break;");    
    http_printf(req, "            case %d:",HTTP_AUTO_LSTOP_ID);
    http_printf(req, "              this.showStillButton = true;");    
    http_printf(req, "              this.showNoBodyButton = true;");
    http_printf(req, "              this.showSoBodyButton = true;");
    http_printf(req, "              this.showStopButton = false;");
    http_printf(req, "              this.isLearnStillDisabled = false;");    
    http_printf(req, "              this.isLearnSoBodyDisabled = false;");
    http_printf(req, "              this.isLearnNoBodyDisabled = false;");
    http_printf(req, "              this.isStopDisabled = true;");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_DEBUG_SOMEBODY_ID);
    http_printf(req, "              this.showAutoLearning = false;");    
    http_printf(req, "              this.showOTAProgressBar = false;");
    http_printf(req, "              this.isDebugSomebodyDisabled = true;");
    http_printf(req, "              this.isDebugOffDisabled = false;");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_DEBUG_NOBODY_ID);
    http_printf(req, "              this.showAutoLearning = false;");    
    http_printf(req, "              this.showOTAProgressBar = false;");
    http_printf(req, "              this.isDebugNobodyDisabled = true;");
    http_printf(req, "              this.isDebugOffDisabled = false;");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_DEBUG_OFF_ID);
    http_printf(req, "              this.showAutoLearning = false;");    
    http_printf(req, "              this.showOTAProgressBar = false;");  
    http_printf(req, "              this.isDebugSomebodyDisabled = false;");
    http_printf(req, "              this.isDebugNobodyDisabled = false;");
    http_printf(req, "              this.isDebugOffDisabled = true;");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_OTA_ID);
    http_printf(req, "              this.showAutoLearning = false;");    
    http_printf(req, "              this.showConfig = false;");
    http_printf(req, "              this.showOTAProgressBar = true;");
    http_printf(req, "              this.isResetDisabled = true;");
    http_printf(req, "              this.isDisplayDisabled = true;");
    http_printf(req, "              this.isSaveDisabled = true;");
    http_printf(req, "              this.isLearnStillDisabled = true;");    
    http_printf(req, "              this.isLearnSoBodyDisabled = true;");
    http_printf(req, "              this.isLearnNoBodyDisabled = true;");
    http_printf(req, "              this.isStopDisabled = true;");
    http_printf(req, "              this.isDebugSomebodyDisabled = true;");
    http_printf(req, "              this.isDebugNobodyDisabled = true;");    
    http_printf(req, "              this.isDebugOffDisabled = true;");
    http_printf(req, "              this.isFirmwareUpgradeDisabled = true;");
    http_printf(req, "              this.isRebootDisabled = true;");
    http_printf(req, "              this.isEraseDataDisabled = true;");
    http_printf(req, "              this.startFirmwareUpgrade();");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_OTA_PROGRESS_ID);
    http_printf(req, "              const percent = data['ota-percent'];");
    http_printf(req, "              this.otaPercent = percent;");
    http_printf(req, "              this.sysFirmwareupgradestatus = data.sysFirmwareupgradestatus || 0;");    
    http_printf(req, "              if ((percent <= 100) && (this.sysFirmwareupgradestatus == %d)){",OTA_IN_PROGRESS);
    http_printf(req, "                this.otaPercentText = percent + '%%';");
    http_printf(req, "              }");
    http_printf(req, "              if (this.sysFirmwareupgradestatus == %d){",OTA_IDLE);
    http_printf(req, "                if (this.otaInterval) {");
    http_printf(req, "                  clearInterval(this.otaInterval);");
    http_printf(req, "                }");
    http_printf(req, "                this.otaPercentText = 'Firmware Upgrade fail';");
    http_printf(req, "              }");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_REBOOT_ID);
    http_printf(req, "              if (this.displayInterval) {");
    http_printf(req, "                clearInterval(this.displayInterval);");
    http_printf(req, "              }");
    http_printf(req, "              this.showAutoLearning = false;");    
    http_printf(req, "              this.showConfig = false;");
    http_printf(req, "              this.showOTAProgressBar = true;");
    http_printf(req, "              this.otaPercent = 100;");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_ERASEDATA_ID);
    http_printf(req, "              if (this.displayInterval) {");
    http_printf(req, "                clearInterval(this.displayInterval);");
    http_printf(req, "              }");
    http_printf(req, "              this.showAutoLearning = false;");    
    http_printf(req, "              this.showConfig = false;");
    http_printf(req, "              this.showOTAProgressBar = true;");
    http_printf(req, "              this.otaPercent = 100;");
    http_printf(req, "              break;");
    http_printf(req, "            case %d:",HTTP_ENV_UPDT);
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        http_printf(req, "              this.mq135currentdata = data.mq135currentdata || 0;");
        http_printf(req, "              this.mq135thresholdhigh = data.mq135thresholdhigh || 0;");
        http_printf(req, "              this.mq135thresholdlow = data.mq135thresholdlow || 0;");
        http_printf(req, "              this.deltafanscheduler = data.deltafanscheduler || 0;");        
    }
    http_printf(req, "              this.dht22currenttemp = data.dht22currenttemp || 0;");
    http_printf(req, "              this.dht22thresholdtemphigh = data.dht22thresholdtemphigh || 0;");
    http_printf(req, "              this.dht22thresholdtemplow = data.dht22thresholdtemplow || 0;");
    http_printf(req, "              this.dht22currenthumi = data.dht22currenthumi || 0;");
    http_printf(req, "              this.dht22thresholdhumihigh = data.dht22thresholdhumihigh || 0;");
    http_printf(req, "              this.dht22thresholdhumilow = data.dht22thresholdhumilow || 0;");
    http_printf(req, "              this.sysLearnstillnessstatus = data.sysLearnstillnessstatus || 0;");    
    http_printf(req, "              this.sysLearnsomebodystatus = data.sysLearnsomebodystatus || 0;");
    http_printf(req, "              this.sysLearnnobodystatus = data.sysLearnnobodystatus || 0;");
#if defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "              this.nuld2410pred = data.nuld2410pred || 0;");
    http_printf(req, "              this.nuld2410new = data.nuld2410new || 0;");
    http_printf(req, "              if(this.nuld2410new)");
    http_printf(req, "              {");
    http_printf(req, "                  this.isSaveDisabled = false;");    
    http_printf(req, "              }");
    http_printf(req, "              else");
    http_printf(req, "              {");
    http_printf(req, "                  this.isSaveDisabled = true;");
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysLearnstillnessstatus)");
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showOTAProgressBar = false;");
    http_printf(req, "                  this.showStillButton = true;");
    http_printf(req, "                  this.showSoBodyButton = false;");    
    http_printf(req, "                  this.showNoBodyButton = false;");    
    http_printf(req, "                  this.showStopButton = true;");    
    http_printf(req, "                  this.isLearnStillDisabled = true;");
    http_printf(req, "                  this.isStopDisabled = false;"); 
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysLearnsomebodystatus)");
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showOTAProgressBar = false;");
    http_printf(req, "                  this.showStillButton = false;");
    http_printf(req, "                  this.showSoBodyButton = true;");    
    http_printf(req, "                  this.showNoBodyButton = false;");
    http_printf(req, "                  this.showStopButton = true;");
    http_printf(req, "                  this.isLearnSoBodyDisabled = true;");
    http_printf(req, "                  this.isStopDisabled = false;"); 
    http_printf(req, "              }");
    http_printf(req, "              if(this.sysLearnnobodystatus)");
    http_printf(req, "              {");
    http_printf(req, "                  this.showAutoLearning = false;");    
    http_printf(req, "                  this.showOTAProgressBar = false;");
    http_printf(req, "                  this.showStillButton = false;");    
    http_printf(req, "                  this.showSoBodyButton = false;");
    http_printf(req, "                  this.showNoBodyButton = true;");
    http_printf(req, "                  this.showStopButton = true;");
    http_printf(req, "                  this.isLearnNoBodyDisabled = true;");
    http_printf(req, "                  this.isStopDisabled = false;"); 
    http_printf(req, "              }");    
    http_printf(req, "              if((this.sysLearnstillnessstatus==0)&&(this.sysLearnsomebodystatus==0)&&(this.sysLearnnobodystatus==0))");
    http_printf(req, "              {");
    http_printf(req, "                  this.showStillButton = true;");    
    http_printf(req, "                  this.showNoBodyButton = true;");
    http_printf(req, "                  this.showSoBodyButton = true;");
    http_printf(req, "                  this.showStopButton = false;");
    http_printf(req, "                  this.isLearnStillDisabled = false;");    
    http_printf(req, "                  this.isLearnSoBodyDisabled = false;");
    http_printf(req, "                  this.isLearnNoBodyDisabled = false;");
    http_printf(req, "                  this.isStopDisabled = true;"); 
    http_printf(req, "              }");
#endif
    http_printf(req, "              break;");
    http_printf(req, "            default:");
    http_printf(req, "              alert(data.error);");
    http_printf(req, "              break;");
    http_printf(req, "          }");
    http_printf(req, "        }");
    http_printf(req, "      })");
    http_printf(req, "      .catch(error => console.error('Error fetching data:', error));");
    http_printf(req, "    },");
    
    http_printf(req, "    initChart() {");
    http_printf(req, "        const ctxENV = document.getElementById(\"EnvironmentChart\").getContext(\"2d\");");
    http_printf(req, "        const ctxNULD2410 = document.getElementById(\"NULD2410Chart\").getContext(\"2d\");");
    http_printf(req, "        this.ENVchart = new Chart(ctxENV, {");
    http_printf(req, "            type: \"line\",");
    http_printf(req, "            data: {");
    http_printf(req, "                labels: [],");
    http_printf(req, "                datasets: [");
    http_printf(req, "                    {");
    http_printf(req, "                        label: \"Temperature (°C)\",");
    http_printf(req, "                        data: [],");
    http_printf(req, "                        borderColor: \"#f44336\",");
    http_printf(req, "                        borderWidth: 2,");
    http_printf(req, "                        fill: false,");
    http_printf(req, "                        tension: 0.4");
    http_printf(req, "                    },");
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {        
        http_printf(req, "                    {");
        http_printf(req, "                        label: \"Air Quality\",");
        http_printf(req, "                        data: [],");
        http_printf(req, "                        borderColor: \"#FFB74D\",");
        http_printf(req, "                        borderWidth: 2,");
        http_printf(req, "                        fill: false,");
        http_printf(req, "                        tension: 0.4");        
        http_printf(req, "                    },");
    }
    http_printf(req, "                    {");
    http_printf(req, "                        label: \"Humidity (%%)\",");
    http_printf(req, "                        data: [],");
    http_printf(req, "                        borderColor: \"#2196f3\",");
    http_printf(req, "                        borderWidth: 2,");
    http_printf(req, "                        fill: false,");
    http_printf(req, "                        tension: 0.4");
    http_printf(req, "                    }");
    http_printf(req, "                ]");
    http_printf(req, "            },");
    http_printf(req, "            options: {");
    http_printf(req, "                responsive: true,");
    http_printf(req, "                plugins: {");
    http_printf(req, "                    legend: { position: 'top' },");
    http_printf(req, "                    customText: {}");
    http_printf(req, "                },");
    http_printf(req, "                scales: {");
    http_printf(req, "                    x: { title: { display: true, text: \"Time\" } },");
    http_printf(req, "                    y: { title: { display: true, text: \"Value\" } }");
    http_printf(req, "                }");
    http_printf(req, "            },");
    http_printf(req, "            plugins: [");
    http_printf(req, "                {");
    http_printf(req, "                    id: 'customText',");
    http_printf(req, "                    beforeDraw: (chart) => {");
    http_printf(req, "                        const { ctx, chartArea: { top, right } } = chart;");
    http_printf(req, "                        ctx.save();");
    http_printf(req, "                        ctx.font = \"14px Arial\";");
    http_printf(req, "                        ctx.fillStyle = \"#555\";");
    http_printf(req, "                        ctx.textAlign = \"right\";");
    http_printf(req, "                        const fanFlags = this.deltafanscheduler || [];");
    http_printf(req, "                        const statusList = [];");

    http_printf(req, "                        if (fanFlags[0] === 2) statusList.push(\"Manual\");");
    http_printf(req, "                        if (fanFlags[1] === 2) statusList.push(\"Exhaust\");");
    http_printf(req, "                        if (fanFlags[2] === 2) statusList.push(\"Warm\");");
    http_printf(req, "                        if (fanFlags[3] === 2) statusList.push(\"Dry\");");
    http_printf(req, "                        if (fanFlags[4] === 2) statusList.push(\"HomeKit\");");

    http_printf(req, "                        const displayText = \"Fan Status: \" + (statusList.length > 0 ? statusList.join(\", \") : \"Off\");");

    http_printf(req, "                        ctx.fillText(displayText, right - 10, top + 20);");
    http_printf(req, "                        ctx.restore();");
    http_printf(req, "                    }");
    http_printf(req, "                 }");
    http_printf(req, "            ]");
    http_printf(req, "        });");

    http_printf(req, "        this.NULD2410chart = new Chart(ctxNULD2410, {");
    http_printf(req, "            type: \"bar\",");
    http_printf(req, "            data: {");
    http_printf(req, "                labels: [],");
    http_printf(req, "                datasets: [");
    http_printf(req, "                    {");
    http_printf(req, "                        label: \"Prediction\",");
    http_printf(req, "                        data: [],");
    //http_printf(req, "                        borderColor: \"#f44336\",");
    http_printf(req, "                        backgroundColor: [],");
    http_printf(req, "                        borderWidth: 1");
    //http_printf(req, "                        fill: false,");
    //http_printf(req, "                        tension: 0.4");
    http_printf(req, "                    },");
    http_printf(req, "                ]");
    http_printf(req, "            },");
    http_printf(req, "            options: {");
    http_printf(req, "                responsive: true,");
    http_printf(req, "                scales: {");
    http_printf(req, "                    x: { title: { display: true, text: \"Time\" } },");
    http_printf(req, "                    y: { title: { display: true, text: \"Value\" }, min:0, max:1 }");
    http_printf(req, "                }");
    http_printf(req, "            }");
    http_printf(req, "        });");

    http_printf(req, "    },");
    http_printf(req, "updateAirQuality() {");
    http_printf(req, "    this.fetchData(%d);",HTTP_ENV_UPDT);
    http_printf(req, "    const now = new Date().toLocaleTimeString();");
    http_printf(req, "    this.temperatureData.push(this.dht22currenttemp);");
    http_printf(req, "    this.humidityData.push(this.dht22currenthumi);");
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        http_printf(req, "    this.airQualityData.push(this.mq135currentdata);");
    }
    http_printf(req, "    this.airQualityLabels.push(now);");
    http_printf(req, "    if (this.humidityData.length > %d) {",AQ_HIS_DUR/AQ_HIS_REF_TIME);
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {    
        http_printf(req, "        this.airQualityData.shift();");
    }
    http_printf(req, "        this.temperatureData.shift();");
    http_printf(req, "        this.humidityData.shift();");
    http_printf(req, "        this.airQualityLabels.shift();");
    http_printf(req, "    }");

    http_printf(req, "    this.ENVchart.data.labels = this.airQualityLabels;");
    http_printf(req, "    this.ENVchart.data.datasets[0].data = this.temperatureData;");
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    { 
        http_printf(req, "    this.ENVchart.data.datasets[1].data = this.airQualityData;");
        http_printf(req, "    this.ENVchart.data.datasets[2].data = this.humidityData;");
    }
    else
    {
        http_printf(req, "    this.ENVchart.data.datasets[1].data = this.humidityData;");
    }
    http_printf(req, "    this.ENVchart.update();");
    http_printf(req, "},");
    
    http_printf(req, "updateNULD2410() {");
    http_printf(req, "    this.fetchData(%d);",HTTP_ENV_UPDT);
    http_printf(req, "    const now = new Date().toLocaleTimeString();");
    http_printf(req, "    this.nuld2410predData.push(this.nuld2410pred);");
    http_printf(req, "    this.NULD2410Labels.push(now);");
    http_printf(req, "    if (this.nuld2410predData.length > %d) {",PRED_HIS_DUR/PRED_HIS_REF_TIME);
    http_printf(req, "        this.nuld2410predData.shift();");
    http_printf(req, "        this.NULD2410Labels.shift();");
    http_printf(req, "    }");

    http_printf(req, "    this.NULD2410chart.data.labels = this.NULD2410Labels;");
    http_printf(req, "    this.NULD2410chart.data.datasets[0].data = this.nuld2410predData;");
    http_printf(req, "    this.NULD2410chart.data.datasets[0].backgroundColor = this.nuld2410predData.map(value => { const hue = 240 + (120 * value);return `hsl(${hue}, 100%%, 40%%)`;});");
    http_printf(req, "    this.NULD2410chart.update();");
    http_printf(req, "},");

    http_printf(req, "    updateLevelsByFacility() {");
    http_printf(req, "      const selected = this.getSelectedFacilities();");
    http_printf(req, "      const statusList = Array.isArray(this.syslogstatus) ? this.syslogstatus : [];");    
    http_printf(req, "      if (selected.length === 0) {");
    http_printf(req, "        this.form.facility = '';"); 
    http_printf(req, "        this.form.level0 = false;");
    http_printf(req, "        this.form.level1 = false;");
    http_printf(req, "        this.form.level2 = false;");
    http_printf(req, "        this.form.level3 = false;");
    http_printf(req, "        this.form.level4 = false;");
    http_printf(req, "        return;");
    http_printf(req, "      }");
    http_printf(req, "      this.form.facility = String(selected[0]);");
    http_printf(req, "      const allHaveBit = (bit) => selected.every((id) => {");
    http_printf(req, "        const status = statusList[id] || 0;");
    http_printf(req, "        return !!(status & (1 << bit));");
    http_printf(req, "      });");
    http_printf(req, "      this.form.level0 = allHaveBit(0);");
    http_printf(req, "      this.form.level1 = allHaveBit(1);");
    http_printf(req, "      this.form.level2 = allHaveBit(2);");
    http_printf(req, "      this.form.level3 = allHaveBit(3);");
    http_printf(req, "      this.form.level4 = allHaveBit(4);");
    http_printf(req, "    },");

    http_printf(req, "    updateLocalLogLevel() {");
    http_printf(req, "      const selected = this.getSelectedFacilities();");
    http_printf(req, "      if (!Array.isArray(this.syslogstatus)) {");
    http_printf(req, "        this.syslogstatus = [];");
    http_printf(req, "      }");
    http_printf(req, "      const targets = selected.length ? selected : this.getSelectedFacilities(true);");
    http_printf(req, "      if (targets.length === 0) {");
    http_printf(req, "        return;");
    http_printf(req, "      }");
    http_printf(req, "      const mask = (this.form.level0 << 0)|(this.form.level1 << 1)|(this.form.level2 << 2)|(this.form.level3 << 3)|(this.form.level4 << 4);");
    http_printf(req, "      targets.forEach((id) => {");
    http_printf(req, "        if (id < 0) {");
    http_printf(req, "          return;");
    http_printf(req, "        }");
    http_printf(req, "        while (this.syslogstatus.length <= id) {");
    http_printf(req, "          this.syslogstatus.push(0);");
    http_printf(req, "        }");
    http_printf(req, "        this.syslogstatus[id] = mask;");
    http_printf(req, "      });");
    http_printf(req, "      this.updateLevelsByFacility();");
    http_printf(req, "    },");

    http_printf(req, "    getSelectedFacilities(includeFallback = false) {");
    http_printf(req, "      const selection = this.form.facilities;");
    http_printf(req, "      let list = [];");
    http_printf(req, "      if (Array.isArray(selection) && selection.length) {");
    http_printf(req, "        list = selection;");
    http_printf(req, "      } else if (!Array.isArray(selection) && selection) {");
    http_printf(req, "        list = [selection];");
    http_printf(req, "      }");
    http_printf(req, "      let ids = list.map((value) => parseInt(value, 10)).filter((value) => !Number.isNaN(value));");
    http_printf(req, "      if (includeFallback && ids.length === 0 && this.form.facility !== '') {");
    http_printf(req, "        const fallback = parseInt(this.form.facility, 10);");
    http_printf(req, "        if (!Number.isNaN(fallback)) {");
    http_printf(req, "          ids = [fallback];");
    http_printf(req, "        }");
    http_printf(req, "      }");
    http_printf(req, "      return ids;");
    http_printf(req, "    },");

    http_printf(req, "    submitForm() {");
    http_printf(req, "      const selectedTargets = this.getSelectedFacilities(true);");
    http_printf(req, "      const facilityList = selectedTargets.join(',');");
    http_printf(req, "      const basePayload = Object.assign({}, this.form);");
    http_printf(req, "      delete basePayload.facilities;");
    http_printf(req, "      const payload = Object.assign({}, basePayload, {");
    http_printf(req, "        facility: selectedTargets.length ? String(selectedTargets[0]) : '',");
    http_printf(req, "        facilityList: facilityList");
    http_printf(req, "      });");
    http_printf(req, "      fetch('/submitform', {");
    http_printf(req, "        method: 'POST',");
    http_printf(req, "        headers: { 'Content-Type': 'application/json' },");
    http_printf(req, "        body: JSON.stringify(payload),");
    http_printf(req, "      })");
    http_printf(req, "      .then((response) => response.text())");
    http_printf(req, "      .then((data) => {");
    http_printf(req, "        console.log('Response from server:', data);");
    http_printf(req, "      })");
    http_printf(req, "      .catch((error) => console.error('Error submitting form:', error));");
    http_printf(req, "    },");
    http_printf(req, "    startDisplayAutoLearn() {");
    http_printf(req, "      console.log('Starting Auto Learn...');"); // Add debug log
    // Stop any existing interval to avoid multiple intervals
    http_printf(req, "      if (this.displayInterval) {");
    http_printf(req, "        this.showConfig = true;");
    http_printf(req, "        this.showAutoLearning = false;");
    http_printf(req, "        clearInterval(this.displayInterval);");
    http_printf(req, "        this.displayInterval = 0");
#if defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "        this.bottonTextDisplay = \"Syslog Input Data\";");
#else   
    http_printf(req, "        this.bottonTextDisplay = \"Display Result\";");
#endif
    http_printf(req, "      }else{");
    // Start a new interval to fetch OTA progress
    http_printf(req, "        this.showConfig = false;");
    http_printf(req, "        this.showAutoLearning = true;");
#if defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "        this.bottonTextDisplay = \"Syslog Input Data\";");
#else   
    http_printf(req, "        this.bottonTextDisplay = \"Hide Result\";");
#endif
    http_printf(req, "        this.displayInterval = setInterval(() => {");
    http_printf(req, "        this.fetchData('%d');",HTTP_AUTO_DISPLAY_LOOP_ID);
    http_printf(req, "      }, 2000);}");
    http_printf(req, "    },");
    http_printf(req, "    startFirmwareUpgrade() {");
    http_printf(req, "      console.log('Starting firmware upgrade...');"); // Add debug log
    // Stop any existing interval to avoid multiple intervals
    http_printf(req, "      if (this.otaInterval) {");
    http_printf(req, "        clearInterval(this.otaInterval);");
    http_printf(req, "      }");
    http_printf(req, "      if (this.displayInterval) {");
    http_printf(req, "        clearInterval(this.displayInterval);");
    http_printf(req, "      }");
    // Start a new interval to fetch OTA progress
    http_printf(req, "      this.otaInterval = setInterval(() => {");
    http_printf(req, "        this.fetchData('%d');",HTTP_OTA_PROGRESS_ID);
    http_printf(req, "      }, 500);");
    http_printf(req, "    },");
    http_printf(req, "    startCountdown() {");
    http_printf(req, "      this.otaCountdown = 15;"); // 15 seconds countdown
    http_printf(req, "      const countdownInterval = setInterval(() => {");
    http_printf(req, "        if (this.otaCountdown > 0) {");
    http_printf(req, "          this.otaCountdown--;");
    http_printf(req, "          this.otaPercentText = `Rebooting, Please Wait (${this.otaCountdown} sec.)`;"); // 更新倒计时文本
    http_printf(req, "        } else {");
    http_printf(req, "          clearInterval(countdownInterval);");
    http_printf(req, "          location.reload();");
    http_printf(req, "        }");
    http_printf(req, "      }, 1000);");
    http_printf(req, "    },");
    http_printf(req, "    getColorForPercentage(percent) {");
    // Calculate red, green, and blue components
    http_printf(req, "      const red = Math.round(206 - (percent * 206) / 100);");
    http_printf(req, "      const green = Math.round(206 - (percent * 206) / 100);");
    http_printf(req, "      const blue = 255;"); // 0-255    
    // Convert to a hex string
    http_printf(req, "      const color = (red << 16) | (green << 8) | blue;");
    http_printf(req, "      return '#' + color.toString(16).padStart(6, '0');");
    http_printf(req, "    }");
    http_printf(req, "  },");
    http_printf(req, "  watch: {");
    http_printf(req, "    otaPercent(newVal) {");
    http_printf(req, "      if (newVal >= 100) {");
    http_printf(req, "        clearInterval(this.otaInterval);");
    //http_printf(req, "        this.otaPercentText = 'Rebooting, Please Wait...';");
    http_printf(req, "        this.startCountdown();");
    http_printf(req, "      } else {");
    http_printf(req, "        this.otaPercentText = newVal + '%%';");
    http_printf(req, "      }");
    http_printf(req, "    }");
    http_printf(req, "  }");
    http_printf(req, "});");
}

static void http_view_modle(httpd_req_t * req)
{
    int i = 0, temphigh = 0, templow = 0, humihigh = 0, humilow = 0;
    int mq135thresholdhigh = 0, mq135thresholdlow = 0;
    uint32_t delaytime = 0;
    char orgtpapikey[THINGSPEAK_API_KEYLENGTH+1] = {0};
    uint8_t sys_mac[6];
    int leddisplaytime = 0, ledsnoozetime = 0;
    char ota_ip[OTA_MAXLEN_IP+1],ota_filename[OTA_MAXLEN_FILENAME+1], syslog_server_ip[SYSLOG_MAXLEN_IP+1];

    memset(ota_ip,0,sizeof(ota_ip));
    memset(ota_filename,0,sizeof(ota_filename));
    memset(syslog_server_ip,0,sizeof(syslog_server_ip));

    ota_getip(ota_ip,OTA_MAXLEN_IP);
    ota_getfilename(ota_filename,OTA_MAXLEN_FILENAME);
    syslog_get_server_ip(syslog_server_ip, SYSLOG_MAXLEN_IP);

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    /* New Vue */
    http_printf(req, "new Vue({");
    http_printf(req, "  el: '#app',");
    http_printf(req, "  data: {");
    http_printf(req, "    sysBuildversion: '',"); /* Build version */
    http_printf(req, "    sysBuildTime: '',"); /* Build version */
    http_printf(req, "    sysMacaddress: '',"); /* MACaddress */
    http_printf(req, "    sysIPaddress: '',"); /* IPaddress */
    http_printf(req, "    sysOffthreshold: 0,"); /* Off threshold */
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        http_printf(req, "    mq135currentdata: 0,"); /* Current Air quality */
        http_printf(req, "    mq135thresholdhigh: 0,"); /* Worst Air quality threshold */
        http_printf(req, "    mq135thresholdlow: 0,"); /* Best Air quality threshold */
        http_printf(req, "    airQualityData: [],"); /* Recently Air quality data */
        http_printf(req, "    deltafanscheduler: [],"); /* Best Air quality threshold */
    }
    http_printf(req, "    airQualityLabels: [],"); /* Air quality Time labels */
    http_printf(req, "    NULD2410Labels: [],"); /* Artificial Neural Network Time labels */
    http_printf(req, "    dht22currenttemp: 0,"); /* Current Temperature */
    http_printf(req, "    dht22thresholdtemphigh: 0,"); /* High Temperature threshold */
    http_printf(req, "    dht22thresholdtemplow: 0,"); /* Low Temperature threshold */
    http_printf(req, "    dht22currenthumi: 0,"); /* Current Humidity */
    http_printf(req, "    dht22thresholdhumihigh: 0,"); /* High Humidity threshold */
    http_printf(req, "    dht22thresholdhumilow: 0,"); /* Low Humidity threshold */
#if defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "    nuld2410pred: 0,"); /* Artificial Neural Network Prediction */
    http_printf(req, "    nuld2410new: 0,"); /* ANN saved weight is not latest */
    http_printf(req, "    nuld2410predData: [],"); /* Recently Artificial Neural Network Prediction data */
#endif
    http_printf(req, "    temperatureData: [],");
    http_printf(req, "    humidityData: [],");
    http_printf(req, "    syslogstatus: [],"); /* Air quality Time labels */
    http_printf(req, "    sysResetstatus: 0,"); /* Reset Status */
    http_printf(req, "    sysDisplaySsatus: 0,"); /* Display Status */
    http_printf(req, "    sysSavestatus: 0,"); /* Save Status*/
    http_printf(req, "    sysLearnstillnessstatus: 0,"); /* Learn Stillness Status */
    http_printf(req, "    sysLearnsomebodystatus: 0,"); /* Learn Somebody Status */
    http_printf(req, "    sysLearnnobodystatus: 0,"); /* Learn Nobody Statys */
    http_printf(req, "    sysDebugsomebodystatus: 0,"); /* Debug Somebody Status */
    http_printf(req, "    sysDebugnobodystatus: 0,"); /* Debug Nobody Status */
    http_printf(req, "    sysFirmwareupgradestatus: 0,"); /* Firmware Upgrade Status */
    http_printf(req, "    sysRebootstatus: 0,"); /* Reboot Status */
    http_printf(req, "    sysErasestatus: 0,"); /* Erase Date Status */    
    http_printf(req, "    bottonTextDisplay: '',"); /* For display different text of button */    
    http_printf(req, "    otaPercent: 0,");
    http_printf(req, "    otaPercentText: '0%%',");
    http_printf(req, "    otaInterval: null,");
    http_printf(req, "    otaCountdown: 0,");
    http_printf(req, "    displayInterval: null,");
    http_printf(req, "    showAutoLearning: false,");
    http_printf(req, "    showNoBodyButton: true,");
    http_printf(req, "    showSoBodyButton: true,");
    http_printf(req, "    showStillButton: true,");
    http_printf(req, "    showStopButton: false,");
    http_printf(req, "    showDis1: true,");
    http_printf(req, "    showOTAProgressBar: false,");
    http_printf(req, "    showConfig: true,");
    http_printf(req, "    isResetDisabled: false,");
    http_printf(req, "    isDisplayDisabled: false,");
    http_printf(req, "    isSaveDisabled: true,");
    http_printf(req, "    isLearnStillDisabled: true,");
    http_printf(req, "    isLearnSoBodyDisabled: false,");
    http_printf(req, "    isLearnNoBodyDisabled: false,");
    http_printf(req, "    isStopDisabled: true,");
    http_printf(req, "    isDebugSomebodyDisabled: false,");
    http_printf(req, "    isDebugNobodyDisabled: false,");
    http_printf(req, "    isDebugOffDisabled: true,");
    http_printf(req, "    isFirmwareUpgradeDisabled: false,");
    http_printf(req, "    isRebootDisabled: false,");
    http_printf(req, "    isEraseDataDisabled: false,");
    http_printf(req, "    isEraseDataDisabled: false,");
    http_printf(req, "    autolearnInterval: null,");
#if !defined(LD2410_AUTOLEARN_NU)
    http_printf(req, "    auto_learn_result:[],");
    http_printf(req, "    auto_learn_counter:[],");
    http_printf(req, "    auto_learn_maxdis: 0,");
    http_printf(req, "    auto_learn_maxdis: 0,");
    http_printf(req, "    auto_learn_nbtal: 0,");
    http_printf(req, "    auto_learn_sbtal: 0,");
#endif
    http_printf(req, "    selectedDoors: [],");  /* For save selected door */
    http_printf(req, "    form: {");
    http_printf(req, "      door: '',");
    http_printf(req, "      activeSensitivity: '',");
    http_printf(req, "      staticSensitivity: '',");
    ld2410_getLeaveDelayTime(&delaytime);
    http_printf(req, "      idelTimes: \"%d\",",delaytime);    
    http_printf(req, "      syslogIp: \"%s\",",syslog_server_ip);
    http_printf(req, "      firmwareIp: \"%s\",",ota_ip);
    http_printf(req, "      firmwareFilename: \"%s\",",ota_filename);
    thingspeak_getapikey(orgtpapikey,THINGSPEAK_API_KEYLENGTH);
    http_printf(req, "      apikey: \"%s\",",orgtpapikey);
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        airquality_get_voc_threshold_high(&mq135thresholdhigh);
        airquality_get_voc_threshold_low(&mq135thresholdlow);
        http_printf(req, "      mq135high: \"%d\",",mq135thresholdhigh);
        http_printf(req, "      mq135low: \"%d\",",mq135thresholdlow);
    }
    
    dht22_gethightemperature(&temphigh);
    dht22_getlowtemperature(&templow);
    dht22_gethighhumidity(&humihigh);
    dht22_getlowhumidity(&humilow);
    oled_getDisplayTime(&leddisplaytime);
    oled_getSnoozeTime(&ledsnoozetime);
    http_printf(req, "      temphigh: \"%d\",",temphigh);
    http_printf(req, "      templow: \"%d\",",templow);
    http_printf(req, "      humihigh: \"%d\",",humihigh);
    http_printf(req, "      humilow: \"%d\",",humilow);
    http_printf(req, "      leddisplay: \"%d\",",leddisplaytime);
    http_printf(req, "      ledsnooze: \"%d\",",ledsnoozetime);
    http_printf(req, "      facilities: [],");
    http_printf(req, "      facility: '',");
    for(i=0;i<SYSLOG_LEVEL_MAXNUM;i++)
    {
        http_printf(req, "      level%d: false,",i);
    }
    http_printf(req, "    }");
    http_printf(req, "  },");
}

static void http_view_mounted(httpd_req_t *req)
{
    /* Mounted */
    http_printf(req, "  mounted() {");
    http_printf(req, "if (this.$el) {");
        http_printf(req, "this.$el.classList.remove('hidden');");
    http_printf(req, "}");
    http_printf(req, "this.fetchData(%d);",HTTP_LOADING_ID); /* Fetch data when web loading */
    http_printf(req, "              this.initChart();");
    http_printf(req, "              this.updateInterval = setInterval(this.updateAirQuality, %d);",AQ_HIS_REF_TIME);
    http_printf(req, "              this.updateLearnInterval = setInterval(this.updateNULD2410, %d);",PRED_HIS_REF_TIME);    
    http_printf(req, "  },");
    http_printf(req, "beforeDestroy() {");
    http_printf(req, "clearInterval(this.updateInterval);");
    http_printf(req, "clearInterval(this.updateLearnInterval);");
    http_printf(req, "},");
}
#endif  // legacy_http_view_helpers
