/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "syslog.h"
#include "system.h"
#include "lwip/sockets.h"

char gsyslog_server_ip[SYSLOG_MAXLEN_IP+1] = "192.168.50.137";
char gsyslog_facility_str[SYSLOG_FACILITY_MAXNUM][SYSLOG_MAXLEN_FACILITY_STR+1] = {"AirQuality","ANN","Elf","Homekit","HTTP","Humidity","IR","MAX9814","Occupancy","OLED","OTA","RMT","SNTP","Syslog","System","Telnet","Temperature","ThingSpeak","Web"};
char gsyslog_level_str[SYSLOG_LEVEL_MAXNUM][SYSLOG_MAXLEN_LEVEL_STR+1] = {"Debug","Info","Warning","Alert","Error"};

static const char *TAG_SYSLOG = "Syslog";
static void syslog_send(const char *message);
char gsyslog_switch[SYSLOG_FACILITY_MAXNUM]={SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT,SYSLOG_LEVEL_DEFAULT};

SemaphoreHandle_t gsemaSyslog = NULL;

int syslog_get_facility_level(uint32_t facility, uint32_t* level)
{
    if(gsemaSyslog==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_ERROR,"Semaphore not ready (syslog %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(level==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_ERROR,"Get level pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaSyslog, portMAX_DELAY) == pdTRUE) 
    {
        *level = gsyslog_switch[facility];
        xSemaphoreGive(gsemaSyslog);
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_DEBUG,"Get facility %d level %d",facility,level);        
    }
    return SYSTEM_ERROR_NONE;
}

int syslog_set_facility_level(uint32_t facility, uint32_t level)
{
    if(gsemaSyslog==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_ERROR,"Semaphore not ready (syslog %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaSyslog, portMAX_DELAY) == pdTRUE) 
    {
        gsyslog_switch[facility] = level;
        xSemaphoreGive(gsemaSyslog);
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_DEBUG,"Set facility %d level %d",facility,level);        
    }
    return SYSTEM_ERROR_NONE;
}

int syslog_set_server_ip(char *ip, int len)
{
    if(gsemaSyslog==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_ERROR,"Semaphore not ready (syslog %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(ip==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_ERROR,"Set ip pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaSyslog, portMAX_DELAY) == pdTRUE) 
    {
        strncpy(gsyslog_server_ip,ip,len>SYSLOG_MAXLEN_IP?SYSLOG_MAXLEN_IP:len);
        xSemaphoreGive(gsemaSyslog);
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_DEBUG,"Set ip %s",ip);
    }
    return SYSTEM_ERROR_NONE;
}

int syslog_get_server_ip(char *ip, int len)
{
    if(gsemaSyslog==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_ERROR,"Semaphore not ready (syslog %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(ip==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_ERROR,"Get ip pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaSyslog, portMAX_DELAY) == pdTRUE) 
    {
        strncpy(ip, gsyslog_server_ip,len>SYSLOG_MAXLEN_IP?SYSLOG_MAXLEN_IP:len);        
        xSemaphoreGive(gsemaSyslog);
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_DEBUG,"Get ip %s",ip);
    }
    return SYSTEM_ERROR_NONE;
}

int syslog_set_level_status(uint32_t facility, uint32_t level, bool status)
{
    if(gsemaSyslog==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_ERROR,"Semaphore not ready (syslog %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaSyslog, portMAX_DELAY) == pdTRUE) 
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG, SYSLOG_LEVEL_DEBUG,"Set facility %d level %d status %d",facility,level,status);
        if(status)
        {
            gsyslog_switch[facility] = gsyslog_switch[facility] | 0x01<<level;
        }
        else
        {
            gsyslog_switch[facility] = gsyslog_switch[facility] & ~(0x01<<level);
        }
        xSemaphoreGive(gsemaSyslog);
    }
    return SYSTEM_ERROR_NONE;
}

void syslog_handler(uint32_t facility, uint32_t level, const char *fmt, ...)
{
    char msg_buf[256]={0};
    char final_buf[512]={0};
    bool bsend = false;
    bool blog = false;
    va_list args;

    if (gsemaSyslog==NULL)
        return;

    if (xSemaphoreTake(gsemaSyslog, portMAX_DELAY) == pdTRUE) 
    {
        if ((gsyslog_switch[facility] & (char)(0x01 << level)) )
        {
            bsend = true;
        }
        
        /*if((facility==SYSLOG_FACILITY_WEB||facility==SYSLOG_FACILITY_ANN)&&level==SYSLOG_LEVEL_DEBUG)
        {
            blog = true;
        }*/
        xSemaphoreGive(gsemaSyslog);

        if(!bsend&&!blog)
        {
            return;
        }
        
        va_start(args, fmt);
        int len = vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
        va_end(args);

        snprintf(final_buf, sizeof(final_buf), "<%s><%s> %s", gsyslog_facility_str[facility],gsyslog_level_str[level], msg_buf);

        if (len > 0)
        {
            if(blog)
            {
                printf("%s\n",final_buf);
            }
            if(bsend)
            {
                syslog_send(final_buf);
            }
        }
    }
    return;    
}

void syslog_restoreconfig(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    unsigned int value1 = 0;
    char serverip[16] = {};
    size_t read;
    size_t size;
    FILE *f;
    
    gsemaSyslog = xSemaphoreCreateBinary();
    if(gsemaSyslog==NULL)
    {
        return;
    }
    ret = nvs_open(SYSLOG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(gsemaSyslog);
        return;
    }

    value1 = sizeof(serverip);
    ret = nvs_get_str(nvs_handle, SYSLOG_NVS_SERVER_IP, serverip, &value1);
    if ((ret == ESP_OK))
    {
        memcpy(gsyslog_server_ip,serverip,sizeof(gsyslog_server_ip));
    }
    else
    {
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);

    size = sizeof(gsyslog_switch);
    f = fopen(SYSLOG_CFG_PATH, "rb");
    if(!f) 
    {
        ret = false;
    }
    else
    {
        read = fread(gsyslog_switch, sizeof(char), SYSLOG_FACILITY_MAXNUM, f);
        if(read != SYSLOG_FACILITY_MAXNUM) 
        {
            ret = false;
        }
        fclose(f);
    }
    xSemaphoreGive(gsemaSyslog);
    return;
}

void syslog_saveconfig(char *key, char *serverip)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    FILE *f;
    size_t written;

    ret = nvs_open(SYSLOG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        syslog_handler(SYSLOG_FACILITY_SYSLOG,SYSLOG_LEVEL_ERROR,"NVS open failed for name %s error %s",SYSLOG_NVS_NAMESPACE,esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_str(nvs_handle, key, serverip);
    if (ret != ESP_OK) {
        syslog_handler(SYSLOG_FACILITY_SYSLOG,SYSLOG_LEVEL_ERROR,"NVS set failed for key %s error %s",key,esp_err_to_name(ret));
    }    
    nvs_close(nvs_handle);

    f = fopen(SYSLOG_CFG_PATH, "wb");
    if (!f) 
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG,SYSLOG_LEVEL_ERROR,"File open %s error",SYSLOG_CFG_PATH);
        return;
    }
    written = fwrite(gsyslog_switch, sizeof(char), SYSLOG_FACILITY_MAXNUM, f);
    fclose(f);
    if (written != SYSLOG_FACILITY_MAXNUM) 
    {
        syslog_handler(SYSLOG_FACILITY_SYSLOG,SYSLOG_LEVEL_ERROR,"Save data size error");
        return;
    }
    syslog_handler(SYSLOG_FACILITY_SYSLOG,SYSLOG_LEVEL_INFO,"Config saved");
    return;
}

static void syslog_send(const char *message)
{
    int sock;
    struct sockaddr_in server_addr;

    if(system_isrebooting())
    {
        return;
    }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG_SYSLOG, "Error creating socket");
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SYSLOG_SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(gsyslog_server_ip);

    if (sendto(sock, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG_SYSLOG, "Error sending message");
    }

    close(sock);
}