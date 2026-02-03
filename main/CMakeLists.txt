set(srcs 
    "adc_helper.c"
    "airquality.c"
    "app_main.c"
    "ir_hta_encoder.c"
    "ir_zro_encoder.c"
    "ir_delta_encoder.c"
    "ir_dyson_encoder.c"
    "dht22.c"
    "homekit.c"
    "ld2410.c"
    "max9814.c"
    "mq135.c"
    "nu_ld2410.c"
    "oled.c"
    "ota.c"
    "rmt.c"
    "sntp.c"
    "syslog.c"
    "system.c"
    "telnet.c"
    "thingspeak.c"
    "task_monitor.c"
    "webpages.c"
)

if(CONFIG_SGP41_ENABLE)
    list(APPEND srcs "sgp41.c")
endif()

idf_component_register(SRCS ${srcs}
                        INCLUDE_DIRS "."
                        REQUIRES dht qrcode esp_netif esp_event esp_wifi nvs_flash esp_adc app_hap_setup_payload ssd1306 spiffs app_update esp_http_client button app_wifi esp_hap_apple_profiles esp_hap_extras esp_https_ota
                        EMBED_TXTFILES server.pem)
