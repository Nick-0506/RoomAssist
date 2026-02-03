/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app_hap_setup_payload.h>
#include <string.h>
#include "dht22.h"
#include "ld2410.h"
#include "system.h"
#include "syslog.h"
#include "homekit.h"
#include "esp_timer.h"
#include "rmt.h"
#include "ir_delta_encoder.h"
#include "esp_wifi.h"

float gtemperature;
float ghumidity;
int gtemphigh = DHT22_DEFAULT_TEMP_HI_THOLD;
int gtemplow = DHT22_DEFAULT_TEMP_LO_THOLD;
int ghumihigh = DHT22_DEFAULT_HUMI_HI_THOLD;
int ghumilow = DHT22_DEFAULT_HUMI_LO_THOLD;
float ghumidityhistorydata[DHT22_SW_CFG_HISTORYTIME/DHT22_SW_CFG_IDELTIME] = {0};
int ghumidityhistoryidx = 0;
int ghumidityhistoryisfull = false;

esp_timer_handle_t gsync_delta_dry_timer_handle = NULL;
esp_timer_handle_t gsync_delta_warm_timer_handle = NULL;
SemaphoreHandle_t gsemaDHT22Cfg = NULL;

static void dht22_restoreconfig(void);

int dht22_getcurrenttemperature(float *value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(value==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Get temperature pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gtemperature;
        xSemaphoreGive(gsemaDHT22Cfg);
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_DEBUG,"Get temperature %f",*value);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_setcurrenttemperature(float value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        gtemperature = value;
        xSemaphoreGive(gsemaDHT22Cfg);
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_DEBUG,"Set temperature %f",value);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_getcurrenthumidity(float *value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(value==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Get humidity pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = ghumidity;
        xSemaphoreGive(gsemaDHT22Cfg);
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_DEBUG,"Get humidity %f",*value);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_setcurrenthumidity(float value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }    
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        ghumidity = value;
        xSemaphoreGive(gsemaDHT22Cfg);
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_DEBUG,"Set humidity %f",value);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_gethightemperature(int *value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(value==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Get high temperature threshold pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gtemphigh;
        xSemaphoreGive(gsemaDHT22Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_getlowtemperature(int *value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }    
    if(value==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Get low temperature threshold pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = gtemplow;
        xSemaphoreGive(gsemaDHT22Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_sethightemperature(int value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }    
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        gtemphigh = value;
        xSemaphoreGive(gsemaDHT22Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_setlowtemperature(int value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }    
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        gtemplow = value;
        xSemaphoreGive(gsemaDHT22Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_gethighhumidity(int *value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(value==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Get high humidity threshold pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }    
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = ghumihigh;
        xSemaphoreGive(gsemaDHT22Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_getlowhumidity(int *value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(value==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Get low humidity threshold pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    } 
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        *value = ghumilow;
        xSemaphoreGive(gsemaDHT22Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

int dht22_sethighhumidity(int value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }    
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        ghumihigh = value;
        xSemaphoreGive(gsemaDHT22Cfg);
    }
    return SYSTEM_ERROR_NONE;    
}

int dht22_setlowhumidity(int value)
{
    if(gsemaDHT22Cfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HUMIDITY,SYSLOG_LEVEL_ERROR,"Semaphore not ready (dht22 %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }    
    if (xSemaphoreTake(gsemaDHT22Cfg, portMAX_DELAY) == pdTRUE) 
    {
        ghumilow = value;
        xSemaphoreGive(gsemaDHT22Cfg);
    }
    return SYSTEM_ERROR_NONE;
}

void task_dht22(void *arg)
{
    int err_cnt = 0;
    float humidifydiff = 0.0;
    float temperature = 0.0;
    float humidity = 0.0;
    uint8_t sys_mac[6];

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    esp_timer_create_args_t sync_delta_dry_timer_args = {
        .callback = &ir_sync_delta_dry_timer_callback,
        .name = "sync_delta_dry_timer"
    };
    esp_timer_create_args_t sync_delta_warm_timer_args = {
        .callback = &ir_sync_delta_warm_timer_callback,
        .name = "sync_delta_warm_timer"
    };
    // Restore configuration
    dht22_restoreconfig();

    // Inform DHT22 task completed
    system_task_created(TASK_DHT22_ID);

    // Waiting other task completed
    system_task_all_ready();
    
    while (1) {
        if(err_cnt>=DHT22_MAX_ERROR_TIMES)
        {
            break;
        }
        if (dht_read_float_data(DHT_TYPE_AM2301, DHT22_GPIO_NUM, &humidity, &temperature) == ESP_OK) 
        {
            dht22_setcurrenttemperature(temperature);
            if(!IS_BATHROOM(sys_mac))
            {
                hap_update_value(HAP_ACCESSORY_HITACHI_AC, HAP_CHARACTER_CUR_TEMPERATURE, &temperature);
            }
            if(IS_BATHROOM(sys_mac))
            {
                hap_update_value(HAP_ACCESSORY_TEMPERATURE, HAP_CHARACTER_CUR_TEMPERATURE, &temperature);
            }
            dht22_setcurrenthumidity(humidity);
            hap_update_value(HAP_ACCESSORY_HUMIDITY, HAP_CHARACTER_CUR_TEMPERATURE, &humidity);
            err_cnt = 0;

            ghumidityhistorydata[ghumidityhistoryidx++] = humidity;
            if(ghumidityhistoryidx==DHT22_SW_CFG_HISTORYTIME/DHT22_SW_CFG_IDELTIME)
            {
                ghumidityhistoryidx = 0;
                ghumidityhistoryisfull = true;
            }

            if(ghumidityhistoryisfull)
            {
                humidifydiff = ghumidityhistorydata[(ghumidityhistoryidx==0)?(DHT22_SW_CFG_HISTORYTIME/DHT22_SW_CFG_IDELTIME)-1:ghumidityhistoryidx-1]-ghumidityhistorydata[ghumidityhistoryidx];
            }
#if 0
            if((humidity>=ghumihigh && rmt_isdryfanactive()==0)&&(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac)))
#else
            if((IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))&&
                rmt_isdryfanactive()==0 &&
                (humidity>=ghumihigh||(humidifydiff>15.0&&ghumidityhistoryisfull)))
#endif            
            {
                if((ld2410_isOccupancyStatus())&&(temperature<=gtemplow))
                {
                    uint64_t duration = ((uint64_t)IR_DELTA_FAN_DURATION_1HR)*60*60*1000*1000;
                    syslog_handler(SYSLOG_FACILITY_TEMPERATURE, SYSLOG_LEVEL_INFO,"There are someone and temperature/humidity is %.1f/%.1f, turn on warm fan", temperature, humidity);
                    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_WARM, IR_DELTA_FAN_TIGGER_ACTIVE_ON,IR_DELTA_FAN_DURATION_1HR);
                    if (gsync_delta_warm_timer_handle!=NULL)
                    {
                        if(esp_timer_is_active(gsync_delta_warm_timer_handle))
                        {
                            syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_DEBUG,"Stop warm timer");
                            ESP_ERROR_CHECK(esp_timer_stop(gsync_delta_warm_timer_handle));
                        }
                    }
                    else
                    {
                        syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_DEBUG,"Create warm timer");
                        ESP_ERROR_CHECK(esp_timer_create(&sync_delta_warm_timer_args, &gsync_delta_warm_timer_handle));
                    }
                    // Set working time is not forever, start timer
                    syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_DEBUG,"Start warm timer %llu",duration);
                    ESP_ERROR_CHECK(esp_timer_start_once(gsync_delta_warm_timer_handle, duration));
                    rmt_setwarmfanstatus(true);
                }
                else
                {
                    uint64_t duration = ((uint64_t)IR_DELTA_FAN_DURATION_3HR)*60*60*1000*1000;
                    syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_INFO,"Humidity is %.1f, turn on dry fan", ghumidity);
                    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_DRY, IR_DELTA_FAN_TIGGER_ACTIVE_ON,IR_DELTA_FAN_DURATION_3HR);
                    if (gsync_delta_dry_timer_handle!=NULL)
                    {
                        if(esp_timer_is_active(gsync_delta_dry_timer_handle))
                        {
                            syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_DEBUG,"Stop dry timer");
                            ESP_ERROR_CHECK(esp_timer_stop(gsync_delta_dry_timer_handle));
                        }
                    }
                    else
                    {
                        syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_DEBUG,"Create dry timer");
                        ESP_ERROR_CHECK(esp_timer_create(&sync_delta_dry_timer_args, &gsync_delta_dry_timer_handle));
                    }
                    // Set working time is not forever, start timer
                    syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_DEBUG,"Start dry timer %llu",duration);
                    ESP_ERROR_CHECK(esp_timer_start_once(gsync_delta_dry_timer_handle, duration));
                    rmt_setdryfanstatus(true);
                }
            }
#if 0            
            if((ghumidity<ghumilow && (rmt_iswarmfanactive()||rmt_isdryfanactive()))&&(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac)))
#else
            if((IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))&&
                ((ghumidity<ghumilow)))
#endif                        
            {
                if(rmt_iswarmfanactive())
                {
                    syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_INFO,"Shower complete, turn off warm fan");
                    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_WARM, IR_DELTA_FAN_TIGGER_ACTIVE_OFF, IR_DELTA_FAN_DURATION_3HR);
                    if (gsync_delta_warm_timer_handle!=NULL)
                    {
                        if(esp_timer_is_active(gsync_delta_warm_timer_handle))
                        {
                            syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_DEBUG,"Stop warm timer");
                            ESP_ERROR_CHECK(esp_timer_stop(gsync_delta_warm_timer_handle));
                        }
                    }
                    rmt_setwarmfanstatus(false);
                }
                if(rmt_isdryfanactive())
                {
                    syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_INFO,"Humidity is %.1f, turn off dry fan", ghumidity);
                    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_DRY, IR_DELTA_FAN_TIGGER_ACTIVE_OFF, IR_DELTA_FAN_DURATION_3HR);
                    if (gsync_delta_dry_timer_handle!=NULL)
                    {
                        if(esp_timer_is_active(gsync_delta_dry_timer_handle))
                        {
                            syslog_handler(SYSLOG_FACILITY_HUMIDITY, SYSLOG_LEVEL_DEBUG,"Stop dry timer");
                            ESP_ERROR_CHECK(esp_timer_stop(gsync_delta_dry_timer_handle));
                        }
                    }
                    rmt_setdryfanstatus(false);
                }
            }
        } else {
            syslog_handler(SYSLOG_FACILITY_TEMPERATURE, SYSLOG_LEVEL_ERROR,"Failed to read data from DHT22");
            err_cnt++;
        }

        // Get temperature and humidity per 60 seconds
        vTaskDelay(pdMS_TO_TICKS(DHT22_SW_CFG_IDELTIME));  
    }
    
    syslog_handler(SYSLOG_FACILITY_TEMPERATURE, SYSLOG_LEVEL_ERROR,"Can't Get Date from DHT22 3 times, delete task");
    vTaskDelete(NULL);
}

static void dht22_restoreconfig(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    int32_t value1 = 0;
    
    gsemaDHT22Cfg = xSemaphoreCreateBinary();
    if(gsemaDHT22Cfg == NULL)
    {
        return;
    }

    ret = nvs_open(DHT22_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(gsemaDHT22Cfg);
        return;
    }

    ret = nvs_get_i32(nvs_handle, DHT22_NVS_TEMP_THRESHOLD_HIGH_KEY, &value1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS get failed for key[%s]: %s", DHT22_NVS_TEMP_THRESHOLD_HIGH_KEY, esp_err_to_name(ret));
    }
    else
    {
        gtemphigh = value1;
    }

    value1 = 0;
    ret = nvs_get_i32(nvs_handle, DHT22_NVS_TEMP_THRESHOLD_LOW_KEY, &value1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS get failed for key[%s]: %s", DHT22_NVS_TEMP_THRESHOLD_LOW_KEY, esp_err_to_name(ret));
    }
    else
    {
        gtemplow = value1;
    }

    ret = nvs_get_i32(nvs_handle, DHT22_NVS_HUMI_THRESHOLD_HIGH_KEY, &value1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS get failed for key[%s]: %s", DHT22_NVS_HUMI_THRESHOLD_HIGH_KEY, esp_err_to_name(ret));
    }
    else
    {
        ghumihigh = value1;
    }

    value1 = 0;
    ret = nvs_get_i32(nvs_handle, DHT22_NVS_HUMI_THRESHOLD_LOW_KEY, &value1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS get failed for key[%s]: %s", DHT22_NVS_HUMI_THRESHOLD_LOW_KEY, esp_err_to_name(ret));
    }
    else
    {
        ghumilow = value1;
    }

    nvs_close(nvs_handle);
    xSemaphoreGive(gsemaDHT22Cfg);
    return;
}

void dht22_saveconfig(char *key, int32_t data)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(DHT22_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE, SYSLOG_LEVEL_ERROR,"NVS open failed: %s", esp_err_to_name(ret));
        return;
    }

    int32_t value1 = data;
    ret = nvs_set_i32(nvs_handle, key, value1);
    if (ret != ESP_OK) {
        syslog_handler(SYSLOG_FACILITY_TEMPERATURE, SYSLOG_LEVEL_ERROR,"NVS set failed for key: %s", esp_err_to_name(ret));
    }    
    nvs_close(nvs_handle);
    syslog_handler(SYSLOG_FACILITY_TEMPERATURE, SYSLOG_LEVEL_INFO,"Config saved %s %d", key, data);
    return;
}

void ir_sync_delta_dry_timer_callback()
{
    // Sync Delta fan status to scheduler
    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_INFO,"Timeout, turn off dry fan");
    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_DRY, IR_DELTA_FAN_TIGGER_ACTIVE_OFF,IR_DELTA_FAN_DURATION_FOREVER);
    rmt_setdryfanstatus(false);
    return;
}

void ir_sync_delta_warm_timer_callback()
{
    // Sync Delta fan status to scheduler
    syslog_handler(SYSLOG_FACILITY_IR, SYSLOG_LEVEL_INFO,"Timeout, turn off warm fan");
    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_WARM, IR_DELTA_FAN_TIGGER_ACTIVE_OFF,IR_DELTA_FAN_DURATION_FOREVER);
    rmt_setwarmfanstatus(false);
    return;
}