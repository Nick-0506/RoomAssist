/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "oled.h"
#include "dht22.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "hap.h"
#include "ld2410.h"
#include "airquality.h"
#include "ota.h"
#include "rmt.h"
#include "sdkconfig.h"
#include "sntp.h"
#include "syslog.h"
#include "system.h"
#include "wifi_provisioning/manager.h"
#include <freertos/FreeRTOS.h>
#include <time.h>

#define QR_BUFFER_LEN qrcodegen_BUFFER_LEN_FOR_VERSION(3)

static void oled_restoreconfig(void);

esp_timer_handle_t gled_display_timer_handle = NULL;
int gleddisplaytime = LED_DISPLAY_TIME;
int gledsnoozetime = LED_SNOOZE_TIME;
int gleddisplaymode = LED_DISPLAY_MODE_TIME;
int gpreleddisplaymode = -1;
int gsnoozeloc = 0;
uint8_t temp[QR_BUFFER_LEN];

SemaphoreHandle_t gsemaOLEDCfg = NULL;

int oled_getDisplayMode(int *mode)
{
  if (gsemaOLEDCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_OLED, SYSLOG_LEVEL_DEBUG,
                   "Semaphore not ready (oled %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOLEDCfg, portMAX_DELAY) == pdTRUE)
  {
    *mode = gleddisplaymode;
    xSemaphoreGive(gsemaOLEDCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int oled_setDisplayMode(int mode)
{
  if (gsemaOLEDCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_OLED, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (oled %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOLEDCfg, portMAX_DELAY) == pdTRUE)
  {
    gleddisplaymode = mode;

    if (gled_display_timer_handle != NULL)
    {
      if (esp_timer_is_active(gled_display_timer_handle))
      {
        esp_timer_stop(gled_display_timer_handle);
      }

      int timer_duration = 0;
      switch (mode)
      {
        case LED_DISPLAY_MODE_TIME:
          timer_duration = gleddisplaytime;
          break;
        case LED_DISPLAY_MODE_SNOOZE:
          timer_duration = gledsnoozetime;
          break;
        default:
          timer_duration = 0;
          break;
      }

      if (timer_duration > 0)
      {
        esp_timer_start_once(gled_display_timer_handle,
                             (uint64_t)timer_duration * 1000 * 1000);
      }
    }
    xSemaphoreGive(gsemaOLEDCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int oled_getDisplayTime(int *time)
{
  if (gsemaOLEDCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_OLED, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (oled %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOLEDCfg, portMAX_DELAY) == pdTRUE)
  {
    *time = gleddisplaytime;
    xSemaphoreGive(gsemaOLEDCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int oled_setDisplayTime(int time)
{
  if (gsemaOLEDCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_OLED, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (oled %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOLEDCfg, portMAX_DELAY) == pdTRUE)
  {
    gleddisplaytime = time;
    xSemaphoreGive(gsemaOLEDCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int oled_getSnoozeTime(int *time)
{
  if (gsemaOLEDCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_OLED, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (oled %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOLEDCfg, portMAX_DELAY) == pdTRUE)
  {
    *time = gledsnoozetime;
    xSemaphoreGive(gsemaOLEDCfg);
  }
  return SYSTEM_ERROR_NONE;
}

int oled_setSnoozeTime(int time)
{
  if (gsemaOLEDCfg == NULL)
  {
    syslog_handler(SYSLOG_FACILITY_OLED, SYSLOG_LEVEL_ERROR,
                   "Semaphore not ready (oled %d)", __LINE__);
    return SYSTEM_ERROR_NOT_READY;
  }
  if (xSemaphoreTake(gsemaOLEDCfg, portMAX_DELAY) == pdTRUE)
  {
    gledsnoozetime = time;
    xSemaphoreGive(gsemaOLEDCfg);
  }
  return SYSTEM_ERROR_NONE;
}

void task_oled(void *pvParameter)
{
  SSD1306_t leddev;
  time_t now = 0;
  struct tm timeinfo = {0};
  char out[8][128 / 8 + 3];
  char lloop = 0, dloop = 0;
  int isdot = 0;
  int ip = 0, k = 0, z = 0;
  int pprggress = 0;
  uint8_t raw[17] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  char snoozestr[17];
  uint8_t columnStart[18] = {0xff, 0x80, 0x80, 0x80, 0x80, 0x80,
                             0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                             0x80, 0x80, 0x80, 0x80, 0x80, 0xff};
  uint8_t columnEnd[18] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                           0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                           0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
  uint8_t progbar[14] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  bool wifiprovisioned = true;
  bool bdrawed = false;
  float temperature = 0, humidity = 0;
  uint8_t sys_mac[6];
  int leddisplaytime = 0, orileddisplaymode;
  esp_netif_ip_info_t sys_ip_info;
  int loop_counter = 0;
  int mq135aqi = 0;
  int mq135nox = 0;

  ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
  esp_timer_create_args_t led_display_timer_args = {
      .callback = &led_display_app_timer_callback, .name = "led_display_timer"};

  i2c_master_init(&leddev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
  ssd1306_init(&leddev, 128, 64);

  system_task_created(TASK_OLED_ID);
  ssd1306_clear_screen(&leddev, false);
  ssd1306_contrast(&leddev, 0xff);
  ssd1306_display_text_x2(&leddev, 0, "Booting", strlen("Booting"), false);

  oled_restoreconfig();

  // system_task_all_ready();  // Wait will cause wifi provision password can't
  // display

  if (gled_display_timer_handle != NULL)
  {
    if (esp_timer_is_active(gled_display_timer_handle))
    {
      ESP_ERROR_CHECK(esp_timer_stop(gled_display_timer_handle));
    }
  }
  else
  {
    ESP_ERROR_CHECK(
        esp_timer_create(&led_display_timer_args, &gled_display_timer_handle));
  }

  oled_getDisplayTime(&leddisplaytime);
  ESP_ERROR_CHECK(esp_timer_start_once(gled_display_timer_handle,
                                       (leddisplaytime * 1000 * 1000)));

  while (1)
  {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    loop_counter++;
    if (loop_counter >= 40) loop_counter = 0;
    wifi_prov_mgr_is_provisioned(&wifiprovisioned);
    oled_getDisplayMode(&orileddisplaymode);
    uint8_t ota_status = OTA_DONE;
    if (wifiprovisioned != true && orileddisplaymode != LED_DISPLAY_MODE_OFF)
    {
      oled_setDisplayMode(LED_DISPLAY_MODE_WIFI_PROV);
    }
    else if (hap_get_paired_controller_count() == 0 &&
             orileddisplaymode != LED_DISPLAY_MODE_OFF)
    {
      oled_setDisplayMode(LED_DISPLAY_MODE_HOMEKIT_PAIR);
    }
    if (xSemaphoreTake(gsemaLED, portMAX_DELAY) == pdTRUE)
    {
      ota_getstatus(&ota_status);
      if (ota_status == OTA_IN_PROGRESS)
      {
        ota_getprogress(&pprggress);
        if (pprggress > 0 && pprggress <= 100)
        {
          oled_setDisplayMode(LED_DISPLAY_MODE_FWUG);
          sprintf(out[1], " Progress %3d%%  ", pprggress);

          if (bdrawed == false)
          {
            ssd1306_clear_screen(&leddev, false);
            ssd1306_clear_line(&leddev, 1, false);
            ssd1306_clear_line(&leddev, 2, false);
            ssd1306_clear_line(&leddev, 3, false);
            ssd1306_clear_line(&leddev, 4, false);
            ssd1306_clear_line(&leddev, 5, false);
            ssd1306_display_text(&leddev, 0, "Firmware Upgrade",
                                 strlen("Firmware Upgrade"), false);
            ssd1306_bitmaps(&leddev, 12, 23, columnStart, 8, 18,
                            false); /* Start Line */
            ssd1306_bitmaps(&leddev, 12, 23, raw, 104, 1, false); /* Top Line */
            ssd1306_bitmaps(&leddev, 12, 40, raw, 104, 1,
                            false); /* Bottom Line */
            ssd1306_bitmaps(&leddev, 116, 23, columnEnd, 8, 18,
                            false); /* End Line */
            bdrawed = true;
          }
          ssd1306_display_text(&leddev, 1, out[1], strlen(out[1]), false);
          if (pprggress == 100)
          {
            ssd1306_display_text(&leddev, 1, "  Rebooting...  ",
                                 strlen("  Rebooting...  "), false);
          }

          for (k = ip; k < pprggress; k++)
          {
            if (((k) % 8 == 0) && progbar[0])
            {
              ssd1306_bitmaps(&leddev, (ip + 14), 25, progbar, 8, 14, false);
              memset(progbar, 0, sizeof(progbar));
              ip = k + 1;
            }
            for (int j = 0; j <= 14; j++)
            {
              progbar[j] = progbar[j] | 0x80 >> ((k) % 8); /* Progress */
              if (k >= 96)
              {
                progbar[j] = progbar[j] | 0x04;
              }
            }
          }
          if (progbar[0])
          {
            ssd1306_bitmaps(&leddev, (ip + 14), 25, progbar, 8, 14, false);
          }
        }
      }
      else
      {
        oled_getDisplayMode(&orileddisplaymode);
        if (orileddisplaymode == LED_DISPLAY_MODE_OFF)
        {
          if (gpreleddisplaymode != orileddisplaymode)
          {
            ssd1306_clear_screen(&leddev, false);
          }
        }
        if (orileddisplaymode == LED_DISPLAY_MODE_SNOOZE)
        {
          if (gpreleddisplaymode != orileddisplaymode)
          {
            time(&now);
            localtime_r(&now, &timeinfo);
            gsnoozeloc = timeinfo.tm_sec;
          }
          memset(snoozestr, 0, sizeof(snoozestr));
          ssd1306_clear_screen(&leddev, false);

          switch (gsnoozeloc % 5)
          {
            case 0:
            case 4:
              for (z = 0; z < (gsnoozeloc / 5) % 16; z++)
              {
                strcat(snoozestr, " ");
              }
              strcat(snoozestr, "Z");
              ssd1306_display_text(&leddev, (gsnoozeloc / 5) % 6, snoozestr,
                                   strlen(snoozestr), false);
              if (gsnoozeloc % 5 == 0)
              {
                gsnoozeloc++;
              }
              else
              {
                gsnoozeloc = gsnoozeloc + (timeinfo.tm_sec / 5) + 1;
              }
              break;
            case 1:
            case 3:
              for (z = 0; z < ((gsnoozeloc / 5) % 16) / 2; z++)
              {
                strcat(snoozestr, " ");
              }
              strcat(snoozestr, "Z");
              ssd1306_display_text_x2(&leddev, (gsnoozeloc / 5) % 6, snoozestr,
                                      strlen(snoozestr), false);
              gsnoozeloc++;
              break;
            case 2:
              for (z = 0; z < ((gsnoozeloc / 5) % 16) / 3; z++)
              {
                strcat(snoozestr, " ");
              }
              strcat(snoozestr, "Z");
              ssd1306_display_text_x3(&leddev, (gsnoozeloc / 5) % 6, snoozestr,
                                      strlen(snoozestr), false);
              gsnoozeloc++;
              break;
            default:
              break;
          }
        }

        if (orileddisplaymode == LED_DISPLAY_MODE_TIME)
        {
          int flag = 0;
          char ANType = 0;
          uint8_t scheduler = 0;
          if (gpreleddisplaymode != orileddisplaymode)
          {
            ssd1306_clear_screen(&leddev, false);
            ssd1306_bitmaps(&leddev, 0, 18, raw, 128, 1, false);
          }
          isdot = isdot ? 0 : 1;
          time(&now);
          localtime_r(&now, &timeinfo);
          sprintf(out[3], "%02d%s%02d ", timeinfo.tm_hour, isdot ? ":" : " ",
                  timeinfo.tm_min);
          ssd1306_display_text_x3(&leddev, 3, out[3], strlen(out[3]),
                                  false); /* Time */

          if (loop_counter < 20 ||
              !(IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac)))
          {
            if (loop_counter == 0)
            {
              ssd1306_clear_line(&leddev, 0, false);
              ssd1306_clear_line(&leddev, 1, false);
              // ssd1306_clear_line(&leddev, 2, false);
            }
            dht22_getcurrenttemperature(&temperature);
            dht22_getcurrenthumidity(&humidity);
            sprintf(out[0], " %2d  %2d%%", (int)temperature, (int)humidity);
            out[0][0] = 0x81;
            out[0][3] = 0x80;
            out[0][4] = 0x82;
            ssd1306_display_text_x2(&leddev, 0, out[0], strlen(out[0]),
                                    false); /* Temperature and Humidity */
          }
          else
          {
            if (loop_counter == 20)
            {
              ssd1306_clear_line(&leddev, 0, false);
              ssd1306_clear_line(&leddev, 1, false);
              // ssd1306_clear_line(&leddev, 2, false);
            }
#if CONFIG_SGP41_ENABLE
            airquality_get_voc_index(&mq135aqi);
            airquality_get_nox_index(&mq135nox);
            sprintf(out[0], " %3d %3d", mq135aqi,
                    (mq135nox < 0) ? 0 : mq135nox);
            out[0][0] = 0x94;
            out[0][4] = 0x95;
            ssd1306_display_text_x2(&leddev, 0, out[0], strlen(out[0]), false);
#else
            airquality_get_voc_index(&mq135aqi);
            sprintf(out[0], "AQI: %d", mq135aqi);
            ssd1306_display_text_x2(&leddev, 0, out[0], strlen(out[0]), false);
#endif
          }

          memset(out[6], 0, sizeof(out[6]));
          sprintf(out[6], "            ");
          ld2410_getDebuggingMode(&flag);
          if (flag)
          {
            out[6][0] = 0x83 + dloop;
            dloop++;
            dloop = dloop % 2;
          }
          ld2410_getANType(&ANType);
          if (ANType)
          {
            out[6][1] = 0x85 + lloop;
            lloop++;
            lloop = lloop % 4;
          }
          if (IS_BATHROOM(sys_mac) || IS_SAMPLE(sys_mac))
          {
            ir_get_deltascheduler(IR_DELTA_FAN_TIGGER_MODE_MANUAL, &scheduler);
            if (scheduler > DELTA_FAN_SCHDULER_IDEL)
            {
              out[6][2] = 'M';
            }
            ir_get_deltascheduler(IR_DELTA_FAN_TIGGER_MODE_EXHAUST, &scheduler);
            if (scheduler > DELTA_FAN_SCHDULER_IDEL)
            {
              out[6][3] = 'X';
            }
            ir_get_deltascheduler(IR_DELTA_FAN_TIGGER_MODE_WARM, &scheduler);
            if (scheduler > DELTA_FAN_SCHDULER_IDEL)
            {
              out[6][4] = 'W';
            }
            ir_get_deltascheduler(IR_DELTA_FAN_TIGGER_MODE_DRY, &scheduler);
            if (scheduler > DELTA_FAN_SCHDULER_IDEL)
            {
              out[6][5] = 'D';
            }
            ir_get_deltascheduler(IR_DELTA_FAN_TIGGER_MODE_HOMEKIT, &scheduler);
            if (scheduler > DELTA_FAN_SCHDULER_IDEL)
            {
              out[6][6] = 'H';
            }
          }
          system_get_ip(&sys_ip_info);
          sprintf(out[7], " " IPSTR, IP2STR(&sys_ip_info.ip));
          ssd1306_display_text(&leddev, 6, out[6], strlen(out[6]), false);
          ssd1306_display_text(&leddev, 7, out[7], strlen(out[7]), false);
        }
        if (orileddisplaymode == LED_DISPLAY_MODE_WIFI_PROV)
        {
          if (gpreleddisplaymode != orileddisplaymode)
          {
            ssd1306_clear_screen(&leddev, false);
          }
#ifdef QRCODE_ON_OLED
          uint8_t qrcode[QR_BUFFER_LEN];
          size_t size = 0;

          char provisioning_str[77];
          sprintf(provisioning_str,
                  "{\"ver\":\"v1\",\"name\":\"PROV_%02x%02x%02x\",\"pop\":\"%"
                  "02x%02x%02x%02x\",\"transport\":\"softap\"}",
                  sys_mac[3], sys_mac[4], sys_mac[5], sys_mac[2], sys_mac[3],
                  sys_mac[4], sys_mac[5]);
          dbg_printf("\n wifiprov: %s\n", provisioning_str);
          if (generate_qrcode_data(provisioning_str, qrcode, &size))
          {
            draw_qrcode_to_oled(&leddev, qrcode,
                                size);  // 你先前寫的畫到OLED上的函式
          }
#else
          if (gpreleddisplaymode != orileddisplaymode)
          {
            ssd1306_clear_screen(&leddev, false);
            ssd1306_bitmaps(&leddev, 0, 18, raw, 128, 1, false);
          }
          sprintf(out[0], "%02x%02x%02x%02x", sys_mac[2], sys_mac[3],
                  sys_mac[4], sys_mac[5]);
          ssd1306_display_text_x2(&leddev, 0, "WiFiProv", strlen("WiFiProv"),
                                  false); /* Wi-Fi Provisioning password */
          ssd1306_display_text_x2(&leddev, 3, out[0], strlen(out[0]),
                                  false); /* Wi-Fi Provisioning password */

          memset(out[6], 0, sizeof(out[6]));
          sprintf(out[6], "            ");
#endif
        }
        if (orileddisplaymode == LED_DISPLAY_MODE_HOMEKIT_PAIR)
        {
          if (gpreleddisplaymode != orileddisplaymode)
          {
            ssd1306_clear_screen(&leddev, false);
          }
#ifdef QRCODE_ON_OLED
          uint8_t qrcode[QR_BUFFER_LEN];
          size_t size = 0;
          char provisioning_str[77];
          sprintf(provisioning_str,
                  "{\"ver\":\"v1\",\"name\":\"PROV_%02x%02x%02x\",\"pop\":\"%"
                  "02x%02x%02x%02x\",\"transport\":\"softap\"}",
                  gsys_mac[3], gsys_mac[4], gsys_mac[5], gsys_mac[2],
                  gsys_mac[3], gsys_mac[4], gsys_mac[5]);
          dbg_printf("\n wifiprov: %s\n", provisioning_str);
          if (generate_qrcode_data(provisioning_str, qrcode, &size))
          {
            draw_qrcode_to_oled(&leddev, qrcode,
                                size);  // 你先前寫的畫到OLED上的函式
          }
#else
          if (gpreleddisplaymode != orileddisplaymode)
          {
            ssd1306_clear_screen(&leddev, false);
            ssd1306_bitmaps(&leddev, 0, 18, raw, 128, 1, false);
          }
          snprintf(out[3], sizeof(out[3]), " %.6s", gsetupcode);
          snprintf(out[5], sizeof(out[5]), "  %.3s", gsetupcode + 7);
          ssd1306_display_text_x2(&leddev, 0, "HOMEKIT", strlen("HOMEKIT"),
                                  false); /* Wi-Fi Provisioning password */
          ssd1306_display_text_x2(&leddev, 3, out[3], strlen(out[3]),
                                  false); /* Wi-Fi Provisioning password */
          ssd1306_display_text_x2(&leddev, 5, out[5], strlen(out[5]),
                                  false); /* Wi-Fi Provisioning password */
#endif
        }
      }
      gpreleddisplaymode = orileddisplaymode;
      xSemaphoreGive(gsemaLED);
    }
  }
  vTaskDelete(NULL);
}

void led_display_app_timer_callback()
{
  int leddisplaymode = 0;
  if (xSemaphoreTake(gsemaLED, portMAX_DELAY) == pdTRUE)
  {
    oled_getDisplayMode(&leddisplaymode);
    gpreleddisplaymode = leddisplaymode;
    switch (leddisplaymode)
    {
      case LED_DISPLAY_MODE_OFF:
        /* Turn on OLED and Timer */
        oled_setDisplayMode(LED_DISPLAY_MODE_TIME);
        break;
      case LED_DISPLAY_MODE_TIME:
        /* Display Time timeout  */
        oled_setDisplayMode(LED_DISPLAY_MODE_SNOOZE);
        break;
      case LED_DISPLAY_MODE_SNOOZE:
        /* Display Snooze timeout  */
        oled_setDisplayMode(LED_DISPLAY_MODE_OFF);
        break;
      case LED_DISPLAY_MODE_HOMEKIT_PAIR:
        /* Homekit timeout turn off led */
        oled_setDisplayMode(LED_DISPLAY_MODE_OFF);
        break;
      default:
        break;
    }
    xSemaphoreGive(gsemaLED);
  }
  return;
}

bool generate_qrcode_data(const char *text, uint8_t *qrcode_out,
                          size_t *size_out)
{
  bool ok = 0;

  if (!text || !qrcode_out || !size_out) return false;
  ok = qrcodegen_encodeText(text, temp, qrcode_out, qrcodegen_Ecc_LOW,
                            qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                            qrcodegen_Mask_AUTO, false);
  if (ok)
  {
    *size_out = qrcodegen_getSize(qrcode_out);
  }
  return ok;
}

#ifdef QRCODE_ON_OLED
void draw_qrcode_to_oled(SSD1306_t *dev, const uint8_t *qrcode, int size)
{
  int pixel_size;

  // pixel_size（128x64）
  int max_pixel_x = 128 / size;
  int max_pixel_y = 64 / size;
  // pixel_size = (max_pixel_x < max_pixel_y) ? max_pixel_x : max_pixel_y;

  // if (pixel_size < 1) pixel_size = 1;
  pixel_size = 2;
  int qrcode_width = size * pixel_size;
  int offset_x = (128 - qrcode_width) / 2;
  int offset_y = ((64 - qrcode_width) / 2);

  // Gen QR code
  for (int y = 0; y < size; y++)
  {
    for (int x = 0; x < size; x++)
    {
      if (qrcodegen_getModule(qrcode, x, y))
      {
        for (int dy = 0; dy < pixel_size; dy++)
        {
          for (int dx = 0; dx < pixel_size; dx++)
          {
            _ssd1306_pixel(dev, offset_x + x * pixel_size + dx,
                           offset_y + y * pixel_size + dy, false);
          }
        }
      }
    }
  }

  ssd1306_show_buffer(dev);
}
#endif

static void oled_restoreconfig(void)
{
  nvs_handle_t nvs_handle;
  esp_err_t ret;
  int32_t value1 = 0;

  gsemaOLEDCfg = xSemaphoreCreateBinary();
  if (gsemaOLEDCfg == NULL)
  {
    return;
  }
  ret = nvs_open(OLED_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
    xSemaphoreGive(gsemaOLEDCfg);
    return;
  }

  ret = nvs_get_i32(nvs_handle, OLED_NVS_DISPLAY_KEY, &value1);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG_NVS, "NVS get failed for key[%s]: %s", OLED_NVS_DISPLAY_KEY,
             esp_err_to_name(ret));
  }
  else
  {
    gleddisplaytime = value1;
  }

  value1 = 0;
  ret = nvs_get_i32(nvs_handle, OLED_NVS_SNOOZE_KEY, &value1);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG_NVS, "NVS get failed for key[%s]: %s", OLED_NVS_SNOOZE_KEY,
             esp_err_to_name(ret));
  }
  else
  {
    gledsnoozetime = value1;
  }
  nvs_close(nvs_handle);
  xSemaphoreGive(gsemaOLEDCfg);
  return;
}

void oled_saveconfig(char *key, int32_t data)
{
  nvs_handle_t nvs_handle;
  esp_err_t ret;

  ret = nvs_open(OLED_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
    return;
  }

  int32_t value1 = data;
  ret = nvs_set_i32(nvs_handle, key, value1);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG_NVS, "NVS set failed for key1: %s", esp_err_to_name(ret));
  }
  nvs_close(nvs_handle);
  syslog_handler(SYSLOG_FACILITY_OLED, SYSLOG_LEVEL_INFO, "Config saved %s %d",
                 key, data);
  return;
}
