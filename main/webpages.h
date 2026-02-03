/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>
#include <esp_netif.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
/* For Vue */
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define HTTP_LOADING_ID 001
#define HTTP_AUTO_RESET_ID 101
#define HTTP_AUTO_DISPLAY_ID 102
#define HTTP_AUTO_DISPLAY_LOOP_ID 103
#define HTTP_AUTO_SAVE_ID 104
#define HTTP_AUTO_LSB_ID 201
#define HTTP_AUTO_LNB_ID 202
#define HTTP_AUTO_LSTNS_ID 203
#define HTTP_AUTO_LSTOP_ID 204
#define HTTP_DEBUG_SOMEBODY_ID 301
#define HTTP_DEBUG_NOBODY_ID 302
#define HTTP_DEBUG_OFF_ID 303
#define HTTP_OTA_ID 401
#define HTTP_OTA_PROGRESS_ID (HTTP_OTA_ID + 1)
#define HTTP_OTA_ABORT_ID (HTTP_OTA_ID + 2)
#define HTTP_REBOOT_ID 501
#define HTTP_ERASEDATA_ID 502
#define HTTP_ENV_UPDT 601
#define HTTP_RESET_BASELINE_ID 602
#define HTTP_NU_DOWNLOAD_ID 701
#define HTTP_NU_UPLOAD_ID 702
#define HTTP_ACTION_STATUS_FAIL 0
#define HTTP_ACTION_STATUS_SUCCESS 1

#define AQ_HIS_DUR 120000
#define AQ_HIS_REF_TIME 1500
#define PRED_HIS_DUR 120000
#define PRED_HIS_REF_TIME 1500

#define WEB_INPUT_INIT_VALUE 99

    esp_err_t fetch_vue(httpd_req_t *req);
    esp_err_t handle_submitform(httpd_req_t *req);
    esp_err_t http_homevue(httpd_req_t *req);
    httpd_handle_t http_server_start(void);

#ifdef __cplusplus
}
#endif
