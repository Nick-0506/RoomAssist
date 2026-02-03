/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ota.h"
#include "syslog.h"
#include "system.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// static const char *TAG = "OTA";

char gota_ip[OTA_MAXLEN_IP + 1] = {};
char gota_filename[OTA_MAXLEN_FILENAME + 1] = {};
int gota_content_length = 0;
int gota_total_read_len = 0;
int gota_last_progress = -1;
uint8_t gota_status = OTA_DONE;
QueueHandle_t gqueue_ota;
char server_cert[] = {};
extern const uint8_t server_cert_pem_start[] asm("_binary_server_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_server_pem_end");

SemaphoreHandle_t gsemaOTA = NULL;

static bool ota_build_index_url(const char *firmware_url, char *index_url,
                                size_t url_len);
static esp_err_t
ota_update_index_html(const esp_http_client_config_t *base_cfg);

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
  int ota_last_progress = 0, ota_content_length = 0, ota_total_read_len = 0;
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    break;
  case HTTP_EVENT_ON_CONNECTED:
    break;
  case HTTP_EVENT_HEADER_SENT:
    break;
  case HTTP_EVENT_ON_HEADER:
    if (strcmp(evt->header_key, "Content-Length") == 0) {
      ota_setcontent_len(atoi(evt->header_value));
    }
    break;
  case HTTP_EVENT_ON_DATA:
    ota_gettotal_readlen(&ota_total_read_len);
    ota_total_read_len += evt->data_len;
    ota_settotal_readlen(ota_total_read_len);
    ota_getcontent_len(&ota_content_length);
    if (ota_content_length > 0) {
      ota_getprogress(&ota_last_progress);
      int ota_progress = (ota_total_read_len * 100) / ota_content_length;
      if (ota_progress != ota_last_progress) {
        ota_setprogress(ota_progress);
      }
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    break;
  case HTTP_EVENT_DISCONNECTED:
    break;
  case HTTP_EVENT_REDIRECT:
    break;
  }
  return ESP_OK;
}

void ota_abort(void) {
  nvs_handle_t nvs_handle;
  ota_setcontent_len(0);
  ota_settotal_readlen(0);
  ota_setprogress(-1);
  ota_setstatus(OTA_IDLE);

  esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err == ESP_OK) {
    nvs_set_u8(nvs_handle, OTA_NVS_STATUS_KEY, OTA_IDLE);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
  }
  syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                 "Firmware upgrade failed.");
}

int ota_setstatus(uint8_t value) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    gota_status = value;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "Set status %d",
                   gota_status);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_getstatus(uint8_t *value) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    *value = gota_status;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "Get status %d",
                   *value);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_setprogress(int value) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    gota_last_progress = value;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "Set progress %d",
                   gota_last_progress);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_getprogress(int *value) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    *value = gota_last_progress;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "Get progress %d",
                   *value);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_setcontent_len(int value) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    gota_content_length = value;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG,
                   "Set content len %d", gota_content_length);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_getcontent_len(int *value) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    *value = gota_content_length;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG,
                   "Get content len %d", *value);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_settotal_readlen(int value) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    gota_total_read_len = value;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG,
                   "Set total read len %d", gota_total_read_len);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_gettotal_readlen(int *value) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    *value = gota_total_read_len;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG,
                   "Get total read len %d", *value);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_setfilename(char *name, int len) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (name == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Set filename pointer is invalid");
    return SYSTEM_ERROR_INVALID_POINTER;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    memset(gota_filename, 0, OTA_MAXLEN_FILENAME);
    strncpy(gota_filename, name,
            (len > OTA_MAXLEN_FILENAME) ? OTA_MAXLEN_FILENAME : len);
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "Set filename %s",
                   name);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_getfilename(char *name, int len) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (name == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Get filename pointer is invalid");
    return SYSTEM_ERROR_INVALID_POINTER;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    strncpy(name, gota_filename,
            (len > OTA_MAXLEN_FILENAME) ? OTA_MAXLEN_FILENAME : len);
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "Get filename %s",
                   name);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_setip(char *ip, int len) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (ip == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Set ip pointer is invalid");
    return SYSTEM_ERROR_INVALID_POINTER;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    strncpy(gota_ip, ip, (len > OTA_MAXLEN_IP) ? OTA_MAXLEN_IP : len);
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "Set ip %s", ip);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

int ota_getip(char *ip, int len) {
  if (gsemaOTA == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (ota %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (ip == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Get ip pointer is invalid");
    return SYSTEM_ERROR_INVALID_POINTER;
  }
  if (xSemaphoreTake(gsemaOTA, portMAX_DELAY) == pdTRUE) {
    strncpy(ip, gota_ip, (len > OTA_MAXLEN_IP) ? OTA_MAXLEN_IP : len);
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "Get ip %s", ip);
    xSemaphoreGive(gsemaOTA);
  }
  return SYSTEM_ERROR_NONE;
}

void ota_restoreconfig(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret;
  unsigned int value1 = 0;
  char serverip[OTA_MAXLEN_IP + 1] = {};
  char filename[OTA_MAXLEN_FILENAME + 1] = {};

  gsemaOTA = xSemaphoreCreateBinary();
  if (gsemaOTA == NULL) {
    return;
  }
  ret = nvs_open("OTACFG", NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
    xSemaphoreGive(gsemaOTA);
    return;
  }
  value1 = sizeof(serverip);
  ret = nvs_get_str(nvs_handle, OTA_NVS_SERVER_IP, serverip, &value1);
  if ((ret == ESP_OK)) {
    memcpy(gota_ip, serverip, sizeof(gota_ip));
  } else {
    ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
  }
  value1 = sizeof(filename) - 1;
  ret = nvs_get_str(nvs_handle, OTA_NVS_FILENAME, filename, &value1);
  if ((ret == ESP_OK)) {
    memcpy(gota_filename, filename, sizeof(gota_filename));
  } else {
    ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
  }
  nvs_close(nvs_handle);

  xSemaphoreGive(gsemaOTA);
  return;
}

void ota_saveconfig(char *key, char *str) {
  nvs_handle_t nvs_handle;
  esp_err_t ret;

  ret = nvs_open("OTACFG", NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
    xSemaphoreGive(gsemaOTA);
    return;
  }

  ret = nvs_set_str(nvs_handle, key, str);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_NVS, "NVS set failed for key1: %s", esp_err_to_name(ret));
  }
  nvs_close(nvs_handle);
  syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_INFO, "Config saved %s",
                 key);
  return;
}

static bool ota_build_index_url(const char *firmware_url, char *index_url,
                                size_t url_len) {
  if ((firmware_url == NULL) || (index_url == NULL) || (url_len == 0)) {
    return false;
  }

  const char *query = strchr(firmware_url, '?');
  size_t base_len =
      query ? (size_t)(query - firmware_url) : strlen(firmware_url);
  if (base_len == 0) {
    return false;
  }

  const char *last_slash = NULL;
  for (size_t i = base_len; i > 0; --i) {
    if (firmware_url[i - 1] == '/') {
      last_slash = firmware_url + i - 1;
      break;
    }
  }
  size_t prefix_len = last_slash ? (size_t)(last_slash - firmware_url + 1) : 0;
  const char *file_start = firmware_url + prefix_len;
  size_t file_len = base_len - prefix_len;

  if (file_len == 0 || file_len >= 128) {
    return false;
  }

  char filename[128];
  memcpy(filename, file_start, file_len);
  filename[file_len] = '\0';

  char *dot = strrchr(filename, '.');
  if (dot && strcmp(dot, ".bin") == 0) {
    *dot = '\0';
  }

  const char *dash = strrchr(filename, '-');
  const char *version = (dash && *(dash + 1) != '\0') ? dash + 1 : NULL;

  int written;
  if (version) {
    written =
        snprintf(index_url, url_len, "%.*sindex-%s.html%s", (int)prefix_len,
                 firmware_url, version, query ? query : "");
  } else {
    written = snprintf(index_url, url_len, "%.*sindex.html%s", (int)prefix_len,
                       firmware_url, query ? query : "");
  }

  if ((written <= 0) || ((size_t)written >= url_len)) {
    return false;
  }

  return true;
}

static esp_err_t
ota_update_index_html(const esp_http_client_config_t *base_cfg) {
  if ((base_cfg == NULL) || (base_cfg->url == NULL)) {
    return ESP_ERR_INVALID_ARG;
  }

  char index_url[128] = {};
  if (!ota_build_index_url(base_cfg->url, index_url, sizeof(index_url))) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_WARNING,
                   "Skip HTML update, invalid firmware url %s", base_cfg->url);
    return ESP_FAIL;
  }
  syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG, "HTML url %s",
                 index_url);

  esp_http_client_config_t html_cfg = *base_cfg;
  html_cfg.url = index_url;
  html_cfg.event_handler = NULL;

  esp_http_client_handle_t client = esp_http_client_init(&html_cfg);
  if (client == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Create HTML OTA client failed");
    return ESP_FAIL;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Open HTML OTA url %s failed (%s)", index_url,
                   esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return err;
  }

  int header_status = esp_http_client_fetch_headers(client);
  if (header_status < 0) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "HTML OTA fetch headers failed (%d)", header_status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  int http_status = esp_http_client_get_status_code(client);
  if (http_status != 200) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "HTML OTA unexpected status %d", http_status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  long long content_length = esp_http_client_get_content_length(client);
  if (content_length <= 0) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "HTML OTA invalid content length %lld", content_length);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  FILE *f = fopen("/spiffs/index.html", "w");
  if (f == NULL) {
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "Open /spiffs/index.html failed, errno=%d (%s)", errno,
                   strerror(errno));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  uint8_t buffer[1024];
  size_t total_written = 0;
  esp_err_t read_err = ESP_OK;

  while (true) {
    int read = esp_http_client_read(client, (char *)buffer, sizeof(buffer));
    if (read < 0) {
      int err = errno;
      read_err = ESP_FAIL;
      syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                     "Read HTML OTA data failed, errno=%d (%s)", err,
                     strerror(err));
      break;
    }
    if (read == 0) {
      bool length_ok =
          (content_length <= 0) || ((size_t)content_length == total_written);
      bool complete = esp_http_client_is_complete_data_received(client);
      if (!length_ok) {
        read_err = ESP_FAIL;
        syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                       "HTML OTA data incomplete (written=%u expected=%lld)",
                       (unsigned int)total_written, content_length);
      } else if (!complete) {
        syslog_handler(
            SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_WARNING,
            "HTML OTA completed but http client reports incomplete data");
      }
      break;
    }

    if (fwrite(buffer, 1, read, f) != (size_t)read) {
      int err = errno;
      read_err = ESP_FAIL;
      syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                     "Write HTML OTA data failed, errno=%d (%s)", err,
                     strerror(err));
      break;
    }
    total_written += read;
  }

  fclose(f);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (read_err != ESP_OK) {
    int err = errno;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "HTML OTA transfer failed, errno=%d (%s)", err,
                   strerror(err));
    return read_err;
  }

  if (total_written == 0) {
    int err = errno;
    syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                   "HTML OTA image length is zero, errno=%d (%s)", err,
                   strerror(err));
    return ESP_FAIL;
  }

  syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_DEBUG,
                 "HTML OTA updated %u bytes", (unsigned int)total_written);
  return ESP_OK;
}

void task_ota(void *pvParameter) {
  nvs_handle_t nvs_handle;
  esp_err_t ret;
  int ota_msg = 0;
  char ota_url[64] = {};
  char ota_ip[OTA_MAXLEN_IP + 1], ota_filename[OTA_MAXLEN_FILENAME + 1];
  memset(ota_ip, 0, sizeof(ota_ip));
  memset(ota_filename, 0, sizeof(ota_filename));
  ota_getip(ota_ip, OTA_MAXLEN_IP);
  ota_getfilename(ota_filename, OTA_MAXLEN_FILENAME);
  sprintf(ota_url, "https://%s:8443/%s", ota_ip, ota_filename);

  esp_http_client_config_t config = {
      .url = ota_url,
#ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
  //.crt_bundle_attach = esp_crt_bundle_attach,
#else
  //.cert_pem = (char *)server_cert_pem_start,
#endif /* CONFIG_EXAMPLE_USE_CERT_BUNDLE */
      .event_handler = _http_event_handler,
      .keep_alive_enable = true,
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
      .if_name = &ifr,
#endif
  };

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
  config.skip_cert_common_name_check = true;
#endif
  esp_https_ota_config_t ota_config = {
      .http_config = &config,
  };
  ota_setstatus(OTA_IN_PROGRESS);
  syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_INFO,
                 "Firmware Upgrade from %s.", config.url);
  ret = esp_https_ota(&ota_config);
  if (ret == ESP_OK) {
    esp_err_t html_ret = ota_update_index_html(&config);

    if (html_ret == ESP_OK) {
      ota_setstatus(OTA_DONE);
      ret = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
      if (ret == ESP_OK) {
        nvs_set_u8(nvs_handle, OTA_NVS_STATUS_KEY, gota_status);
        nvs_close(nvs_handle);
      }
      system_task_created(TASK_OTA_ID); /* OTA complete */
      syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_INFO,
                     "Firmware & HTML upgrade complete.");
      while (xQueueReceive(gqueue_ota, &ota_msg, pdMS_TO_TICKS(5000)) !=
             pdPASS) {
        ;
      }
      system_reboot();
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    } else {
      syslog_handler(SYSLOG_FACILITY_OTA, SYSLOG_LEVEL_ERROR,
                     "HTML upgrade failed (%s)", esp_err_to_name(html_ret));
      ota_abort();
      system_task_created(TASK_OTA_ID); /* OTA complete */
    }
  } else {
    ota_abort();
    system_task_created(TASK_OTA_ID); /* OTA complete */
  }

  while (1) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
