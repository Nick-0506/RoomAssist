/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <iot_button.h>
#include <app_wifi.h>
#include <app_hap_setup_payload.h>
#include "homekit.h"
#include "system.h"
#include "syslog.h"
#include "rmt.h"
#include "elf.h"
#include "ld2410.h"
#include "ir_hta_encoder.h"
#include "ir_delta_encoder.h"
#include "ir_zro_encoder.h"
#include "esp_wifi.h"

static int ghomekit_brgaid = 0;
static int ghomekit_osaid = 0;
static int ghomekit_acaid = 0;
static int ghomekit_swaid = 0;
static int ghomekit_fnaid = 0;
static int ghomekit_dfnaid = 0;

int ghomekitidx = 0;

hap_serv_t *ghomekit_oshs = NULL;
hap_serv_t *ghomekit_achs = NULL;
hap_serv_t *ghomekit_swhs = NULL;
hap_serv_t *ghomekit_tshs = NULL;
hap_serv_t *ghomekit_ashs = NULL;
hap_serv_t *ghomekit_hshs = NULL;
hap_serv_t *ghomekit_fnhs = NULL;
hap_serv_t *ghomekit_dfnhs = NULL;
hap_serv_t *ghomekit_clhs = NULL;

hap_char_t *ghomekit_htaac_active_hc = NULL;
hap_char_t *ghomekit_htaac_state_hc = NULL;
hap_char_t *ghomekit_htaac_heating_threshold_hc = NULL;
hap_char_t *ghomekit_htaac_cooling_threshold_hc = NULL;
hap_char_t *ghomekit_htaac_fanspeed_hc = NULL;
hap_char_t *ghomekit_htaac_fanswing_hc = NULL;
hap_char_t *ghomekit_zrofan_active_hc = NULL;
hap_char_t *ghomekit_zrofan_fanspeed_hc = NULL;
hap_char_t *ghomekit_zrofan_fanswing_hc = NULL;
hap_char_t *ghomekit_deltafan_active_hc = NULL;

static char HOMEKIT_SetupCode[10][10]={"111-22-330","111-22-331","111-22-332","111-22-333","111-22-334","111-22-335","111-22-336","111-22-337","111-22-338","111-22-339"};
static char HOMEKIT_SetupId[10][4]={"FH00","FH01","FH02","FH03","FH04","FH05","FH06","FH07","FH08","FH09"};
char gsetupcode[10];

bool gsys_elf_status = ELF_DEFAULT_STATUS;
bool gsys_elf_occupancyfanstatus = false;
SemaphoreHandle_t gsemaHAPCfg = NULL;

static int accessory_identify(hap_acc_t *ha);
static int bridge_identify(hap_acc_t *ha);
static int bridge_identify(hap_acc_t *ha);
static void reset_key_init(uint32_t key_gpio_pin);
static void reset_network_handler(void* arg);
static void reset_to_factory_handler(void* arg);

int hap_update_value(int accessory, int character, void *value)
{
    hap_val_t new_val;

    if(gsemaHAPCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HOMEKIT,SYSLOG_LEVEL_ERROR,"Semaphore not ready (homekit %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if(value==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HOMEKIT,SYSLOG_LEVEL_ERROR,"Get HAP pointer is invalid");
        return SYSTEM_ERROR_INVALID_POINTER;
    }
    if (xSemaphoreTake(gsemaHAPCfg, portMAX_DELAY) == pdTRUE) 
    {
        switch(accessory)
        {
            case HAP_ACCESSORY_OCCUPANCY:
                new_val.i = *(int *)value;
                hap_char_update_val(hap_serv_get_char_by_uuid(ghomekit_oshs, HAP_CHAR_UUID_OCCUPANCY_DETECTED), &new_val); 
                break;
            case HAP_ACCESSORY_HITACHI_AC:
                switch(character)
                {
                    case HAP_CHARACTER_ACTIVE:
                        new_val.i = *(int *)value;
                        hap_char_update_val(ghomekit_htaac_active_hc, &new_val);
                        break;
                    case HAP_CHARACTER_MODE:
                        new_val.i = *(int *)value;
                        hap_char_update_val(ghomekit_htaac_state_hc, &new_val);
                        break;
                    case HAP_CHARACTER_HEATING_THRESHOLD:
                        new_val.f = *(float *)value;
                        hap_char_update_val(ghomekit_htaac_heating_threshold_hc, &new_val);
                        break;
                    case HAP_CHARACTER_COOLING_THRESHOLD:
                        new_val.f = *(float *)value;
                        hap_char_update_val(ghomekit_htaac_cooling_threshold_hc, &new_val);
                        break;
                    case HAP_CHARACTER_FAN_SPEED:
                        new_val.f = *(float *)value;
                        hap_char_update_val(ghomekit_htaac_fanspeed_hc, &new_val);
                        break;
                    case HAP_CHARACTER_FAN_SWING:
                        new_val.i = *(int *)value;
                        hap_char_update_val(ghomekit_htaac_fanswing_hc, &new_val);
                        break;
                    case HAP_CHARACTER_CUR_TEMPERATURE:
                        new_val.f = *(float *)value;
                        hap_char_update_val(hap_serv_get_char_by_uuid(ghomekit_achs, HAP_CHAR_UUID_CURRENT_TEMPERATURE), &new_val);
                        break;
                    default:
                        break;
                }
                break;
            case HAP_ACCESSORY_TEMPERATURE:
                new_val.f = *(float *)value;
                hap_char_update_val(hap_serv_get_char_by_uuid(ghomekit_tshs, HAP_CHAR_UUID_CURRENT_TEMPERATURE), &new_val); 
                break;
            case HAP_ACCESSORY_HUMIDITY:
                new_val.f = *(float *)value;
                hap_char_update_val(hap_serv_get_char_by_uuid(ghomekit_hshs, HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY), &new_val); 
                break;
            case HAP_ACCESSORY_ZERO_FAN:
                switch(character)
                {
                    case HAP_CHARACTER_ACTIVE:
                        new_val.i = *(int *)value;
                        hap_char_update_val(ghomekit_zrofan_active_hc, &new_val);
                        break;
                    case HAP_CHARACTER_FAN_SWING:
                        new_val.i = *(int *)value;
                        hap_char_update_val(ghomekit_zrofan_fanswing_hc, &new_val);
                        break;
                    case HAP_CHARACTER_FAN_SPEED:
                        new_val.i = *(int *)value;
                        hap_char_update_val(ghomekit_zrofan_fanspeed_hc, &new_val);
                        break;
                    default:
                        break;
                }
                break;
            case HAP_ACCESSORY_DELTA_FAN:
                new_val.i = *(int *)value;
                hap_char_update_val(ghomekit_deltafan_active_hc, &new_val);
                break;
            case HAP_ACCESSORY_AIRQUALITY:
                new_val.u = *(uint8_t *)value;
                hap_char_update_val(hap_serv_get_char_by_uuid(ghomekit_ashs, HAP_CHAR_UUID_AIR_QUALITY), &new_val);
                break;
            default:
                break;
        }
        
        xSemaphoreGive(gsemaHAPCfg);
    }
    return SYSTEM_ERROR_NONE;
}

bool hap_iselfactive()
{
    bool ret = false;
    if(gsemaHAPCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HOMEKIT,SYSLOG_LEVEL_ERROR,"Semaphore not ready (homekit %d)",__LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaHAPCfg, portMAX_DELAY) == pdTRUE) 
    {
        ret = gsys_elf_status;
        xSemaphoreGive(gsemaHAPCfg);
    }
    return ret;
}

bool hap_iselfoccupancyfanactive()
{
    bool ret = false;
    if(gsemaHAPCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HOMEKIT,SYSLOG_LEVEL_ERROR,"Semaphore not ready (homekit %d)",__LINE__);
        return false;
    }
    if (xSemaphoreTake(gsemaHAPCfg, portMAX_DELAY) == pdTRUE) 
    {
        ret = gsys_elf_occupancyfanstatus;
        xSemaphoreGive(gsemaHAPCfg);
    }
    return ret;
}

int hap_setelfstatus(int value)
{
    if(gsemaHAPCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HOMEKIT,SYSLOG_LEVEL_ERROR,"Semaphore not ready (homekit %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaHAPCfg, portMAX_DELAY) == pdTRUE) 
    {
        gsys_elf_status = value;
        xSemaphoreGive(gsemaHAPCfg);
    }
    return SYSTEM_ERROR_NONE;
}

int hap_setelfoccupancyfanstatus(int value)
{
    if(gsemaHAPCfg==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_HOMEKIT,SYSLOG_LEVEL_ERROR,"Semaphore not ready (homekit %d)",__LINE__);
        return SYSTEM_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(gsemaHAPCfg, portMAX_DELAY) == pdTRUE) 
    {
        gsys_elf_occupancyfanstatus = value;
        xSemaphoreGive(gsemaHAPCfg);
    }
    return SYSTEM_ERROR_NONE;
}

void ac_saveconfig(char *key, int value)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(AC_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_u32(nvs_handle, key, (uint32_t) value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS set failed for key1: %s", esp_err_to_name(ret));
    }    
    nvs_close(nvs_handle);
    syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"Config saved %s %d",key,value);
    return;
}

/* Mandatory identify routine for the bridged accessory
 * In a real bridge, the actual accessory must be sent some request to
 * identify itself visually
 */
static int accessory_identify(hap_acc_t *ha)
{
    hap_serv_t *hs = hap_acc_get_serv_by_uuid(ha, HAP_SERV_UUID_ACCESSORY_INFORMATION);
    hap_char_t *hc = hap_serv_get_char_by_uuid(hs, HAP_CHAR_UUID_NAME);
    const hap_val_t *val = hap_char_get_val(hc);
    char *name = val->s;

    ESP_LOGI(TAG_HK, "Bridged Accessory %s identified", name);
    return HAP_SUCCESS;
}

static int bridge_identify(hap_acc_t *ha)
{
    ESP_LOGI(TAG_HK, "Bridge identified");
    return HAP_SUCCESS;
}

static int bridge_write(hap_write_data_t write_data[], int count,
        void *serv_priv, void *write_priv)
{
    int i=0, iid=0, aid=0;
    int acval=0;
    const hap_val_t *current_val = 0;
    uint8_t fanspeed=0, actype=0;
    hap_val_t new_val;
    hap_write_data_t *write;
    rmt_hattg_msg_t tghitmsg;
    rmt_zftg_msg_t tgzfmsg;
    int speed = 0, swing = 0;
    uint8_t sys_mac[6];

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
    memset(&tghitmsg,0,sizeof(rmt_hattg_msg_t));
    memset(&tgzfmsg,0,sizeof(rmt_zftg_msg_t));    
    for (i = 0; i < count; i++)
    {
        write = &write_data[i];
        iid = hap_char_get_iid(write->hc);
        aid = hap_acc_get_aid(hap_serv_get_parent(hap_char_get_parent(write->hc)));
        
        if((aid==ghomekit_acaid)&&!(IS_BATHROOM(sys_mac))) // Hitachi AC
        {
            switch(iid)
            {
                case HAP_ACTIVE_OPERATION:   // Active
                    tghitmsg.bactivech = true;
                    acval = (int)write->val.i;
                    /* Received turn off from HAP
                       Switch to fan only 10 min */
                    tghitmsg.active = acval;
                    syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP turn %s Hitachi AC", acval==1?"on":"off");
                    break;
                case HAP_AC_MODE_OPERATION: // Mode
                    tghitmsg.bmodech = true;
                    acval = (int)write->val.i;
                    switch(acval)
                    {
                        case HAP_AC_MODE_AUTO: // Auto
                            actype = HITACHI_AC_MODE_AUTO;
                            syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set Hitachi AC auto mode");
                            break;
                        case HAP_AC_MODE_HEATER: // Warm
                            actype = HITACHI_AC_MODE_HEATER;
                            current_val = hap_char_get_val(ghomekit_htaac_heating_threshold_hc);
                            tghitmsg.blowtempch = true;
                            tghitmsg.lowtemp = (int)current_val->f;
                            syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set Hitachi AC heater mode");
                            break;
                        case HAP_AC_MODE_COOLER: // Cooler
                            actype = HITACHI_AC_MODE_COOLER;
                            current_val = hap_char_get_val(ghomekit_htaac_cooling_threshold_hc);
                            tghitmsg.blowtempch = true;
                            tghitmsg.lowtemp = (int)current_val->f;
                            syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set Hitachi AC cooler mode");
                            break;
                        default:
                            actype = HITACHI_AC_MODE_AUTO;
                            syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_DEBUG,"HAP set Hitachi AC default auto mode");
                            break;
                    }
                    tghitmsg.mode = actype;
                    break;
                case HAP_AC_LOW_TEMP_OPERATION: // Lowest temperature for heater
                    if(actype == HITACHI_AC_MODE_AUTO || count == 1)
                    {
                        tghitmsg.blowtempch = true;
                        tghitmsg.lowtemp = (int)write->val.f;
                        syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set Hitachi AC low temperature for auto mode");
                    }
                    break;
                case HAP_AC_HIGH_TEMP_OPERATION: // Highest temperature for cooler
                    if(actype == HITACHI_AC_MODE_COOLER || count == 1)
                    {
                        tghitmsg.bhightempch = true;
                        tghitmsg.hightemp = (int)write->val.f;
                        syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set Hitachi AC high temperature for cooler mode");
                    }
                    break;
                case HAP_AC_FAN_SPEED_OPERATION: // Fan speed
                    tghitmsg.bfanspeedch=true;
                    acval = (int)write->val.f;
                    fanspeed = (acval/25+(acval%25>0?1:0));
                    fanspeed = (acval==100?HITACHI_AC_AUTO_FAN_SPEED:fanspeed);
                    tghitmsg.fanspeed=fanspeed;
                    syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set Hitachi AC fan speed %d",fanspeed);
                    break;
                case HAP_AC_FAN_SWING_OPERATION: // Fan swing
                    tghitmsg.bswingch=true;
                    acval = (int)write->val.i;
                    tghitmsg.swing = (acval==1?1:0);
                    syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set Hitachi AC fan swing %d",acval);
                    break;
                default:
                    break;
            }
        }
        if(aid==ghomekit_swaid) // Elf
        {
            switch(iid)
            {
                case HAP_ACTIVE_OPERATION:   // on or off
                    acval = (int)write->val.i;
                    if(acval==1)
                    {
                        hap_setelfstatus(true); // on
                        new_val.i = ld2410_isOccupancyStatus();
                        hap_char_update_val(hap_serv_get_char_by_uuid(ghomekit_oshs, HAP_CHAR_UUID_OCCUPANCY_DETECTED), &new_val); 
                        syslog_handler(SYSLOG_FACILITY_ELF, SYSLOG_LEVEL_INFO,"HAP enable Elf");
                    }
                    else
                    {
                        hap_setelfstatus(false); // off
                        syslog_handler(SYSLOG_FACILITY_ELF, SYSLOG_LEVEL_INFO,"HAP disable Elf");
                    }
                    elf_saveconfig(ELF_NVS_STATUS_KEY,hap_iselfactive());
                    break;
                default:
                    break;
            }
        }
        if((aid==ghomekit_fnaid)&&!(IS_BATHROOM(sys_mac))) // +-0 Fan
        {
            switch(iid)
            {
                case HAP_ACTIVE_OPERATION:   // on or off
                    acval = (int)write->val.i;
                    if(acval!=rmt_iszerofanactive())
                    {
                        // Trun On/Off
                        tgzfmsg.bactivech = true;
                        tgzfmsg.active = acval;
                    }
                    syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP turn %s +-0 Fan", acval==1?"on":"off");
                    break;
                case HAP_FAN_FAN_SPEED_OPERATION:   // Fan speed
                    acval = (int)write->val.f;
                    acval = ((acval-1)==0)?0:(((acval-1)/11)+1);
                    acval = (acval>9)?9:acval;
                    rmt_getzerofanspeed(&speed);
                    if(acval!=speed)
                    {
                        tgzfmsg.bfanspeedch = true;
                        tgzfmsg.fanspeed = acval-speed;
                        speed = acval;
                        syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set +-0 Fan fan speed %d",tgzfmsg.fanspeed);
                    }
                    break;
                case HAP_FAN_FAN_SWING_OPERATION:   // Fan Swing
                    acval = (int)write->val.i;
                    rmt_getzerofanswing(&swing);
                    if(acval!=swing)
                    {
                        // On or off
                        tgzfmsg.bswingch = true;
                        tgzfmsg.swing = acval;
                        syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP set +-0 Fan swing %d",tgzfmsg.swing);
                    }
                    break;
                default:
                    break;
            }
        }

        if((aid==ghomekit_dfnaid)&&((IS_BATHROOM(sys_mac))||(IS_SAMPLE(sys_mac)))) // Delta Fan
        {
            switch(iid)
            {
                case HAP_ACTIVE_OPERATION:   // On or off
                    acval = (int)write->val.i;
                    if(acval!=(int)rmt_ismanualfanactive())
                    {
                        syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"HAP turn %s Delta Fan",acval==1?"on":"off");
                        ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_HOMEKIT, acval==1?IR_DELTA_FAN_TIGGER_ACTIVE_ON:IR_DELTA_FAN_TIGGER_ACTIVE_OFF, IR_DELTA_FAN_DURATION_1HR);
                        // Trun On/Off
                        rmt_setmanualfanstatus((bool)acval);
                        deltafan_saveconfig(DELTAFAN_NVS_STATUS_KEY,acval);
                    }
                    break;
                default:
                    break;
            }            
        }
        hap_char_update_val(write->hc, &(write->val));
        *(write->status) = HAP_STATUS_SUCCESS;
    }

    if(tgzfmsg.bactivech||tgzfmsg.bfanspeedch||tgzfmsg.bswingch)
    {
        if(ir_zerofan_tigger(tgzfmsg)==SYSTEM_ERROR_NONE)
        {
            if(tgzfmsg.bactivech)
            {
                rmt_setzerofanstatus((bool)tgzfmsg.active);
                if(ld2410_isOccupancyStatus())
                {
                    /* Record fanstatus when someone here */
                    hap_setelfoccupancyfanstatus(rmt_iszerofanactive());
                    elf_saveconfig(ELF_NVS_OCCUPANCYFANSTATUS_KEY,hap_iselfoccupancyfanactive());
                }
                zerofan_saveconfig(ZEROFAN_NVS_STATUS_KEY,rmt_iszerofanactive());    
            }
            if(tgzfmsg.bfanspeedch)
            {
                rmt_setzerofanspeed(speed);
                zerofan_saveconfig(ZEROFAN_NVS_SPEED_KEY,speed);
            }
            if(tgzfmsg.bswingch)
            {
                rmt_setzerofanswing(tgzfmsg.swing);
                zerofan_saveconfig(ZEROFAN_NVS_SWING_KEY,tgzfmsg.swing);
            }
        }
    }
    if(tghitmsg.bactivech||tghitmsg.bmodech||tghitmsg.bfanspeedch||tghitmsg.bswingch||tghitmsg.blowtempch||tghitmsg.bhightempch)
    {
        if(ir_hitachiac_tigger(tghitmsg)==SYSTEM_ERROR_NONE)
        {
            if(tghitmsg.bactivech)
            {
                rmt_setiracstatus(tghitmsg.active);
                ac_saveconfig(AC_NVS_STATUS_KEY,tghitmsg.active);
            }
            if(tghitmsg.bmodech)
            {
                rmt_setiracmode(actype);
                ac_saveconfig(AC_NVS_TYPE_KEY,actype);
            }
            if(tghitmsg.bfanspeedch)
            {
                rmt_setiracspeed(tghitmsg.fanspeed);
                ac_saveconfig(AC_NVS_SPEED_KEY,tghitmsg.fanspeed);
            }
            if(tghitmsg.bswingch)
            {
                rmt_setiracswing(tghitmsg.swing);
                ac_saveconfig(AC_NVS_SWING_KEY,tghitmsg.swing);
            }
            if(tghitmsg.blowtempch)
            {
                rmt_setiractemp(tghitmsg.lowtemp);
                ac_saveconfig(AC_NVS_TEMP_KEY,tghitmsg.lowtemp);
            }
            if(tghitmsg.bhightempch)
            {
                rmt_setiractemp(tghitmsg.hightemp);
                ac_saveconfig(AC_NVS_TEMP_KEY,tghitmsg.hightemp);
            }
        }
    }
    return HAP_SUCCESS;
}

/**
 * The Reset button  GPIO initialisation function.
 * Same button will be used for resetting Wi-Fi network as well as for reset to factory based on
 * the time for which the button is pressed.
 */
static void reset_key_init(uint32_t key_gpio_pin)
{
    button_handle_t handle = iot_button_create(key_gpio_pin, BUTTON_ACTIVE_LOW);
    iot_button_add_on_release_cb(handle, RESET_NETWORK_BUTTON_TIMEOUT, reset_network_handler, NULL);
    iot_button_add_on_press_cb(handle, RESET_TO_FACTORY_BUTTON_TIMEOUT, reset_to_factory_handler, NULL);
}

static void reset_network_handler(void* arg)
{
    hap_reset_network();
}
/**
 * @brief The factory reset button callback handler.
 */
static void reset_to_factory_handler(void* arg)
{
    hap_reset_to_factory();
}

static void homekit_init_setupcode(uint8_t* sys_mac)
{
    /* 111-22-330 Living room 
       111-22-331 1st bed room
       111-22-332 2nd bed room
       111-22-333 Reading room
       111-22-334 1st bathroom
       111-22-335 tester
       111-22-336 2nd bathroom
     */

    switch(sys_mac[4])
    {
        case 0x8C: // Livingroom
            ghomekitidx = 0;
            break;
        case 0xE1: // 1st bedroom
            ghomekitidx = 1;
            break;
        case 0x23: // 2nd bedroom
            ghomekitidx = 2;
            break;
        case 0x12: // Reading room 
            ghomekitidx = 3;
            break;
        case 0xF0: // Bathroom 1 
            ghomekitidx = 4;
            break;
        case 0xF7: // Bathroom 2
            ghomekitidx = 6;
            break;
        case 0x8D: // Tester
            ghomekitidx = 7;
            break;
        default:
            ghomekitidx = 8;
            break;
    }
}

// The main thread for handling the Bridge Accessory
void task_homekit_init(void *p)
{
    char accessory_name[HAP_MAX_NAME_LENGTH+1] = {0};
    char accessory_seriesnumber[HAP_MAX_SERIESNUMBER_LENGTH+1] = {0};
    char dynamic_setup_id[5] = {};
    hap_acc_t *accessory;
    int state = 0, mode = 0, temp = 0, speed = 0, swing = 0;
    uint8_t sys_mac[6];

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, sys_mac));
#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
    homekit_init_setupcode(sys_mac);
#endif
    // Restore Hitachi Air Control configuration
    if(!IS_BATHROOM(sys_mac))
    {
        ir_ac_restoreconfig();
    }
    
    // Restore +-0 fan configuration
    if(!IS_BATHROOM(sys_mac))
    {
        ir_zerofan_restoreconfig();
    }

    // Restore Delta fan configuration
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        ir_deltafan_restoreconfig();
    }

    // Restore elf configuration
    elf_restoreconfig();
    
    // Initialize the HAP core
    hap_init(HAP_TRANSPORT_WIFI);
    
    /* Initialise the mandatory parameters for Accessory which will be added as
     * the mandatory services internally
     */
    sprintf(accessory_name,"BRG-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
    sprintf(accessory_seriesnumber,"BRGSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
    hap_acc_cfg_t cfg = {
        .name = accessory_name,
        .manufacturer = "Espressif",
        .model = "EspBridge",
        .serial_num = accessory_seriesnumber,
        .fw_rev = "0.9.0",
        .hw_rev = NULL,
        .pv = "1.1.0",
        .identify_routine = bridge_identify,
        .cid = HAP_CID_BRIDGE,
    };
    /* Create accessory object */
    accessory = hap_acc_create(&cfg);

    /* Add a dummy Product Data */
    uint8_t product_data[] = {'E','S','P','3','2','H','A','P'};
    hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

    /* Add Wi-Fi Transport service required for HAP Spec R16 */
    hap_acc_add_wifi_transport_service(accessory, 0);

    /* Add the Accessory to the HomeKit Database */
    hap_add_accessory(accessory);
    ghomekit_brgaid = hap_acc_get_aid(accessory);
    
    /* Create and add the Occupancy Accessory to the Bridge object*/
    {
        memset(accessory_name,0,sizeof(accessory_name));
        memset(accessory_seriesnumber,0,sizeof(accessory_seriesnumber));
        sprintf(accessory_name, "OCC-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        sprintf(accessory_seriesnumber,"OCCSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        hap_acc_cfg_t bridge_cfg = {
            .name = accessory_name,
            .manufacturer = "Hi-Link",
            .model = "LD2410",
            .serial_num = accessory_seriesnumber,
            .fw_rev = "0.9.0",
            .hw_rev = NULL,
            .pv = "1.1.0",
            .identify_routine = accessory_identify,
            .cid = HAP_CID_BRIDGE,
        };
        /* Create accessory object */

        accessory = hap_acc_create(&bridge_cfg);

        /* Create the Occupancy Service. Include the "name" since this is a user visible service  */
        ghomekit_oshs = hap_serv_occupancy_sensor_create(false);

        hap_serv_add_char(ghomekit_oshs, hap_char_name_create(accessory_name)); /* Occ ok*/

        /* Add the Fan Service to the Accessory Object */
        hap_acc_add_serv(accessory, ghomekit_oshs);  /* Occ ok*/

        ghomekit_osaid = hap_get_unique_aid(accessory_name);

        /* Add the Accessory to the HomeKit Database */
        hap_add_bridged_accessory(accessory, hap_get_unique_aid(accessory_name));  /* Occ ok*/

    }
   
    /* Create and add the Air Controller Accessory to the Bridge object*/
    if(!IS_BATHROOM(sys_mac))
    {
        memset(accessory_name,0,sizeof(accessory_name));
        memset(accessory_seriesnumber,0,sizeof(accessory_seriesnumber));
        sprintf(accessory_name, "HAC-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        sprintf(accessory_seriesnumber,"HACSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        hap_acc_cfg_t bridge_cfg = {
            .name = accessory_name,
            .manufacturer = "HITACHI",
            .model = "RAS",
            .serial_num = accessory_seriesnumber,
            .fw_rev = "0.9.0",
            .hw_rev = NULL,
            .pv = "1.1.0",
            .identify_routine = accessory_identify,
            .cid = HAP_CID_BRIDGE,
        };
        /* Create accessory object */
        accessory = hap_acc_create(&bridge_cfg);

        /* Create the AirC Service. Include the "name" since this is a user visible service  */
        state = rmt_isiracactive();
        rmt_getiractemp(&temp);
        rmt_getiracspeed(&speed);
        rmt_getiracswing(&swing);
        rmt_getiracmode(&mode);
        if(!state)
        {
            mode = 0;
        }
        ghomekit_achs = hap_serv_heater_cooler_create(rmt_isiracactive(), temp, mode, mode);
        ghomekit_htaac_active_hc = hap_serv_get_char_by_uuid(ghomekit_achs, HAP_CHAR_UUID_ACTIVE);
        ghomekit_htaac_state_hc = hap_serv_get_char_by_uuid(ghomekit_achs, HAP_CHAR_UUID_TARGET_HEATER_COOLER_STATE);
        ghomekit_htaac_heating_threshold_hc = hap_char_heating_threshold_temperature_create(25.0); //Limitation is 25.0
        ghomekit_htaac_cooling_threshold_hc = hap_char_cooling_threshold_temperature_create(25.0); //Limitation is 25.0
        
        hap_serv_add_char(ghomekit_achs, ghomekit_htaac_heating_threshold_hc);
        hap_serv_add_char(ghomekit_achs, ghomekit_htaac_cooling_threshold_hc);

        // Add fan speed
        ghomekit_htaac_fanspeed_hc = hap_char_rotation_speed_create(speed);
        hap_serv_add_char(ghomekit_achs, ghomekit_htaac_fanspeed_hc);

        // Add fan swing
        ghomekit_htaac_fanswing_hc = hap_char_swing_mode_create(swing);
        hap_serv_add_char(ghomekit_achs, ghomekit_htaac_fanswing_hc);

        hap_serv_add_char(ghomekit_achs, hap_char_name_create(accessory_name));

        /* Set the Accessory name as the Private data for the service,
         * so that the correct accessory can be identified in the
         * write callback
         */
        hap_serv_set_priv(ghomekit_achs, strdup(accessory_name));

        /* Set the write callback for the service */
        hap_serv_set_write_cb(ghomekit_achs, bridge_write);
 
        /* Add the AirC Service to the Accessory Object */
        hap_acc_add_serv(accessory, ghomekit_achs);
        ghomekit_acaid = hap_get_unique_aid(accessory_name);
        /* Add the Accessory to the HomeKit Database */
        hap_add_bridged_accessory(accessory, hap_get_unique_aid(accessory_name));
    }

    /* Create and add the ELD Switch Accessory to the Bridge object*/
    {
        memset(accessory_name,0,sizeof(accessory_name));
        memset(accessory_seriesnumber,0,sizeof(accessory_seriesnumber));
        sprintf(accessory_name, "ELD-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        sprintf(accessory_seriesnumber,"ELFSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        hap_acc_cfg_t bridge_cfg = {
            .name = accessory_name,
            .manufacturer = "Espressif",
            .model = "EspSwitch",
            .serial_num = accessory_seriesnumber,
            .fw_rev = "0.9.0",
            .hw_rev = NULL,
            .pv = "1.1.0",
            .identify_routine = accessory_identify,
            .cid = HAP_CID_BRIDGE,
        };
        /* Create accessory object */
        accessory = hap_acc_create(&bridge_cfg);

        /* Create the Switch. Include the "name" since this is a user visible service  */
        ghomekit_swhs = hap_serv_switch_create(hap_iselfactive());
        
        hap_serv_add_char(ghomekit_swhs, hap_char_name_create(accessory_name)); 

        hap_serv_set_priv(ghomekit_swhs, strdup(accessory_name));

        /* Set the write callback for the service */
        hap_serv_set_write_cb(ghomekit_swhs, bridge_write);
 
        /* Add the AirC Service to the Accessory Object */
        hap_acc_add_serv(accessory, ghomekit_swhs);
        ghomekit_swaid = hap_get_unique_aid(accessory_name);

        /* Add the Accessory to the HomeKit Database */
        hap_add_bridged_accessory(accessory, hap_get_unique_aid(accessory_name));
    }

    /* Create and add the Humidity Accessory to the Bridge object*/
    {
        memset(accessory_name,0,sizeof(accessory_name));
        memset(accessory_seriesnumber,0,sizeof(accessory_seriesnumber));
        sprintf(accessory_name, "HUDS-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        sprintf(accessory_seriesnumber,"HUDSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        hap_acc_cfg_t bridge_cfg = {
            .name = accessory_name,
            .manufacturer = "Aosong Elec.",
            .model = "DHT22",
            .serial_num = accessory_seriesnumber,
            .fw_rev = "0.9.0",
            .hw_rev = NULL,
            .pv = "1.1.0",
            .identify_routine = accessory_identify,
            .cid = HAP_CID_BRIDGE,
        };
        /* Create accessory object */
        accessory = hap_acc_create(&bridge_cfg);

        /* Create the Occupancy Service. Include the "name" since this is a user visible service  */
        ghomekit_hshs = hap_serv_humidity_sensor_create(75.0);
        hap_serv_add_char(ghomekit_hshs, hap_char_status_fault_create(0));
        hap_serv_add_char(ghomekit_hshs, hap_char_name_create(accessory_name));
        hap_acc_add_serv(accessory, ghomekit_hshs);
        hap_add_bridged_accessory(accessory, hap_get_unique_aid(accessory_name));
    }

    /* Create and add the Temperature Accessory to the Bridge object*/
    if(IS_BATHROOM(sys_mac))
    {
        memset(accessory_name,0,sizeof(accessory_name));
        memset(accessory_seriesnumber,0,sizeof(accessory_seriesnumber));
        sprintf(accessory_name, "TMPS-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        sprintf(accessory_seriesnumber,"TMPSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        hap_acc_cfg_t bridge_cfg = {
            .name = accessory_name,
            .manufacturer = "Aosong Elec.",
            .model = "DHT22",
            .serial_num = accessory_seriesnumber,
            .fw_rev = "0.9.0",
            .hw_rev = NULL,
            .pv = "1.1.0",
            .identify_routine = accessory_identify,
            .cid = HAP_CID_BRIDGE,
        };
        /* Create accessory object */
        accessory = hap_acc_create(&bridge_cfg);

        /* Create the Occupancy Service. Include the "name" since this is a user visible service  */
        ghomekit_tshs = hap_serv_temperature_sensor_create(25.0);
        hap_serv_add_char(ghomekit_tshs, hap_char_status_fault_create(0));
        hap_serv_add_char(ghomekit_tshs, hap_char_name_create(accessory_name));
        
        /* Add the temperature Service to the Accessory Object */
        hap_acc_add_serv(accessory, ghomekit_tshs);

        /* Add the Accessory to the HomeKit Database */
        hap_add_bridged_accessory(accessory, hap_get_unique_aid(accessory_name));
    }

    /* Create and add the Air Quality Accessory to the Bridge object*/
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        memset(accessory_name,0,sizeof(accessory_name));
        memset(accessory_seriesnumber,0,sizeof(accessory_seriesnumber));
        sprintf(accessory_name, "AQ-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        sprintf(accessory_seriesnumber,"AQSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        hap_acc_cfg_t bridge_cfg = {
            .name = accessory_name,
            .manufacturer = "Aosong Elec.",
            .model = "MQ135",
            .serial_num = accessory_seriesnumber,
            .fw_rev = "0.9.0",
            .hw_rev = NULL,
            .pv = "1.1.0",
            .identify_routine = accessory_identify,
            .cid = HAP_CID_BRIDGE,
        };
        /* Create accessory object */
        accessory = hap_acc_create(&bridge_cfg);

        /* Create the Occupancy Service. Include the "name" since this is a user visible service  */
        ghomekit_ashs = hap_serv_air_quality_sensor_create(0);
        hap_serv_add_char(ghomekit_ashs, hap_char_name_create(accessory_name));
        
        /* Add the Air quality Service to the Accessory Object */
        hap_acc_add_serv(accessory, ghomekit_ashs);

        /* Add the Accessory to the HomeKit Database */
        hap_add_bridged_accessory(accessory, hap_get_unique_aid(accessory_name));
    }

    /* Create and add the +-0 Fan Accessory to the Bridge object*/
    if(!IS_BATHROOM(sys_mac))
    {
        memset(accessory_name,0,sizeof(accessory_name));
        memset(accessory_seriesnumber,0,sizeof(accessory_seriesnumber));
        sprintf(accessory_name, "ZFan-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        sprintf(accessory_seriesnumber,"ZFNSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        hap_acc_cfg_t bridge_cfg = {
            .name = accessory_name,
            .manufacturer = "+-0",
            .model = "123",
            .serial_num = accessory_seriesnumber,
            .fw_rev = "0.9.0",
            .hw_rev = NULL,
            .pv = "1.1.0",
            .identify_routine = accessory_identify,
            .cid = HAP_CID_BRIDGE,
        };
        /* Create accessory object */
        accessory = hap_acc_create(&bridge_cfg);

        /* Create the +-0 Fan Service. Include the "name" since this is a user visible service  */
        ghomekit_fnhs = hap_serv_fan_v2_create(rmt_iszerofanactive());

        ghomekit_zrofan_active_hc = hap_serv_get_char_by_uuid(ghomekit_fnhs, HAP_CHAR_UUID_ACTIVE);

        hap_serv_add_char(ghomekit_fnhs, hap_char_name_create(accessory_name));
        
        // Add Fan speed
        rmt_getzerofanspeed(&speed);
        ghomekit_zrofan_fanspeed_hc = hap_char_rotation_speed_create(speed*11);
        hap_serv_add_char(ghomekit_fnhs, ghomekit_zrofan_fanspeed_hc);

        // Add Fan swing
        rmt_getzerofanswing(&swing);
        ghomekit_zrofan_fanswing_hc = hap_char_swing_mode_create(swing);
        hap_serv_add_char(ghomekit_fnhs, ghomekit_zrofan_fanswing_hc);

        /* Set the Accessory name as the Private data for the service,
         * so that the correct accessory can be identified in the
         * write callback
         */
        hap_serv_set_priv(ghomekit_fnhs, strdup(accessory_name));

        /* Set the write callback for the service */
        hap_serv_set_write_cb(ghomekit_fnhs, bridge_write);
 
        /* Add the Fan Service to the Accessory Object */
        hap_acc_add_serv(accessory, ghomekit_fnhs);

        ghomekit_fnaid = hap_get_unique_aid(accessory_name);
        /* Add the Accessory to the HomeKit Database */
        hap_add_bridged_accessory(accessory, hap_get_unique_aid(accessory_name));
    }

    /* Create and add the Delta Fan Accessory to the Bridge object*/
    if(IS_BATHROOM(sys_mac)||IS_SAMPLE(sys_mac))
    {
        memset(accessory_name,0,sizeof(accessory_name));
        memset(accessory_seriesnumber,0,sizeof(accessory_seriesnumber));
        sprintf(accessory_name, "DFan-%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        sprintf(accessory_seriesnumber,"DFNSN%02X%02X%02X",sys_mac[3],sys_mac[4],sys_mac[5]);
        hap_acc_cfg_t bridge_cfg = {
            .name = accessory_name,
            .manufacturer = "delta",
            .model = "123",
            .serial_num = accessory_seriesnumber,
            .fw_rev = "0.9.0",
            .hw_rev = NULL,
            .pv = "1.1.0",
            .identify_routine = accessory_identify,
            .cid = HAP_CID_BRIDGE,
        };
        /* Create accessory object */
        accessory = hap_acc_create(&bridge_cfg);

        /* Create the Occupancy Service. Include the "name" since this is a user visible service  */
        ghomekit_dfnhs = hap_serv_fan_v2_create(rmt_ismanualfanactive());

        ghomekit_deltafan_active_hc = hap_serv_get_char_by_uuid(ghomekit_dfnhs, HAP_CHAR_UUID_ACTIVE);

        hap_serv_add_char(ghomekit_dfnhs, hap_char_name_create(accessory_name));
        
        /* Set the Accessory name as the Private data for the service,
         * so that the correct accessory can be identified in the
         * write callback
         */
        hap_serv_set_priv(ghomekit_dfnhs, strdup(accessory_name));

        /* Set the write callback for the service */
        hap_serv_set_write_cb(ghomekit_dfnhs, bridge_write);
 
        /* Add the Fan Service to the Accessory Object */
        hap_acc_add_serv(accessory, ghomekit_dfnhs);

        ghomekit_dfnaid = hap_get_unique_aid(accessory_name);
        /* Add the Accessory to the HomeKit Database */
        hap_add_bridged_accessory(accessory, hap_get_unique_aid(accessory_name));
    }

    /* Register a common button for reset Wi-Fi network and reset to factory.
     */
    reset_key_init(RESET_GPIO);

    /* For production accessories, the setup code shouldn't be programmed on to
     * the device. Instead, the setup info, derived from the setup code must
     * be used. Use the factory_nvs_gen utility to generate this data and then
     * flash it into the factory NVS partition.
     *
     * By default, the setup ID and setup info will be read from the factory_nvs
     * Flash partition and so, is not required to set here explicitly.
     *
     * However, for testing purpose, this can be overridden by using hap_set_setup_code()
     * and hap_set_setup_id() APIs, as has been done here.
     */

#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
    memcpy(gsetupcode,&HOMEKIT_SetupCode[ghomekitidx][0],10);
    memcpy(dynamic_setup_id,&HOMEKIT_SetupId[ghomekitidx][0],4);
    hap_set_setup_code(gsetupcode);
    hap_set_setup_id(dynamic_setup_id);
    app_hap_setup_payload(gsetupcode, dynamic_setup_id, false, cfg.cid);
#endif

    /* Enable Hardware MFi authentication (applicable only for MFi variant of SDK) */
    //hap_enable_mfi_auth(HAP_MFI_AUTH_HW);

    /* After all the initializations are done, start the HAP core */
    hap_start();

    /* Start Wi-Fi */
    app_wifi_start(portMAX_DELAY);

    /* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
    //dbg_printf("\n HOmekit init donw: Brgaid=%d, osaid=%d, acaid=%d, swaid=%d fnaid=%d\n",ghomekit_brgaid,ghomekit_osaid,ghomekit_acaid,ghomekit_swaid,ghomekit_fnaid);
    system_task_created(TASK_HOMEKIT_ID);
    vTaskDelete(NULL);
}

void zerofan_saveconfig(char *key, int value)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(ZEROFAN_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_u32(nvs_handle, key, (uint32_t) value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS set failed for key1: %s", esp_err_to_name(ret));
    }    
    nvs_close(nvs_handle);
    syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"Config saved %s %d",key,value);
    return;
}

void deltafan_saveconfig(char *key, int value)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(DELTAFAN_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_u32(nvs_handle, key, (uint32_t) value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS set failed for key1: %s", esp_err_to_name(ret));
    }    
    nvs_close(nvs_handle);
    syslog_handler(SYSLOG_FACILITY_HOMEKIT, SYSLOG_LEVEL_INFO,"Config saved %s %d",key,value);
    return;
}

void elf_saveconfig(char *key, int value)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(ELF_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_u32(nvs_handle, key, (uint32_t) value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS set failed for key1: %s", esp_err_to_name(ret));
    }    
    nvs_close(nvs_handle);
    return;
}

void elf_restoreconfig(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    uint32_t value1 = 0;
    
    gsemaHAPCfg = xSemaphoreCreateBinary();
    if (gsemaHAPCfg == NULL) 
    {
        return;
    }
    
    ret = nvs_open(ELF_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_NVS, "NVS open failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(gsemaHAPCfg);
        return;
    }

    ret = nvs_get_u32(nvs_handle, ELF_NVS_STATUS_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gsys_elf_status = (bool)value1;
    }
    else
    {
        gsys_elf_status = (bool)ELF_DEFAULT_STATUS;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    ret = nvs_get_u32(nvs_handle, ELF_NVS_OCCUPANCYFANSTATUS_KEY, &value1);
    if ((ret == ESP_OK))
    {
        gsys_elf_occupancyfanstatus = (bool)value1;
    }
    else
    {
        gsys_elf_occupancyfanstatus = false;
        ESP_LOGE(TAG_NVS, "NVS get failed for key-key: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    xSemaphoreGive(gsemaHAPCfg);
    return;
}


