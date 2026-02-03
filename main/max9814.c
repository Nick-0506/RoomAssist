#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_adc/adc_oneshot.h"
#include "adc_helper.h"
#include "system.h"
#include "syslog.h"
#include "esp_timer.h"
#include "esp_dsp.h"
#include "max9814.h"

#ifdef FFT4REAL
#define SAMPLE_RATE 8000         // 8kHz
#define SAMPLE_COUNT (4096)      // 4096 (Must 2^n) 
#else
#define SAMPLE_RATE 8000         // 8kHz
#define SAMPLE_COUNT (1024*4)    // 4096 (Must 2^n)
#endif
#define MAX9814_ADC_UNIT     ADC_UNIT_1
#define MAX9814_ADC_CHANNEL  ADC_CHANNEL_3   // GPIO39
#define MAX9814_ADC_BITWIDTH ADC_BITWIDTH_12
#define MAX9814_ADC_ATTEN    ADC_ATTEN_DB_11
#define MAX9814_ADC_MIDPOINT 2048.0f

bool max9814_is_power_of_4(int n) 
{
    return n > 0 && (n & (n - 1)) == 0 && (n - 1) % 3 == 0;
}

float *pginput_signal = NULL;
float *pgfft_output = NULL;
volatile int sample_index = 0;
esp_timer_handle_t max9814_sample_timer_handle = NULL;
static adc_oneshot_unit_handle_t s_max9814_adc_handle = NULL;
static const char *TAG_MAX9814 = "max9814";

esp_timer_create_args_t max9814_sample_timer4real_args = {
    .callback = &max9814_sample_adc4real,
    .name = "max9814_sample_timer4real"
};

esp_timer_create_args_t max9814_sample_timer_args = {
    .callback = &max9814_sample_adc,
    .name = "max9814_sample_timer"
};

static esp_err_t max9814_adc_init(void)
{
    if (s_max9814_adc_handle != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(adc_helper_config_channel(MAX9814_ADC_UNIT, MAX9814_ADC_CHANNEL,
                        MAX9814_ADC_BITWIDTH, MAX9814_ADC_ATTEN), TAG_MAX9814, "config channel failed");
    ESP_RETURN_ON_ERROR(adc_helper_get_handle(MAX9814_ADC_UNIT, &s_max9814_adc_handle), TAG_MAX9814, "get handle failed");

    return ESP_OK;
}

void max9814_sample_adc4real()
{
    if (pginput_signal == NULL || sample_index >= SAMPLE_COUNT) return;
    int raw = 0;
    if (adc_oneshot_read(s_max9814_adc_handle, MAX9814_ADC_CHANNEL, &raw) != ESP_OK) {
        return;
    }
    *(pginput_signal+sample_index) = (float)raw - MAX9814_ADC_MIDPOINT;
    sample_index=sample_index+1;
}

void max9814_sample_adc()
{
    if (pginput_signal == NULL || sample_index >= SAMPLE_COUNT*2) return;
    int raw = 0;
    if (adc_oneshot_read(s_max9814_adc_handle, MAX9814_ADC_CHANNEL, &raw) != ESP_OK) {
        return;
    }
    *(pginput_signal+sample_index) = (float)raw - MAX9814_ADC_MIDPOINT;
    *(pginput_signal+sample_index+1) = 0.0;
    sample_index=sample_index+2;
}

// Init FFT function
void max9814_init_fft4real() 
{
    int ret = 0;
    if(max9814_is_power_of_4(SAMPLE_COUNT))
    {
        ret = dsps_fft4r_init_fc32(NULL, SAMPLE_COUNT>>1);
        if (ret  != ESP_OK) {
            syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Not possible to initialize FFT4r. Error = %i", ret);
            return;
        }
    }
    else
    {
        ret = dsps_fft2r_init_fc32(NULL, SAMPLE_COUNT>>1);
        if (ret  != ESP_OK) {
            syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Not possible to initialize FFT2r. Error = %i", ret);
            return;
        }
    }
}

void max9814_init_fft() 
{
    int ret = 0;
    ret = dsps_fft2r_init_fc32(NULL, SAMPLE_COUNT);
    if (ret  != ESP_OK) {
        syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Not possible to initialize FFT. Error = %i", ret);
        return;
    }
}

// Calculate FFT
void max9814_compute_fft4real() 
{
    float *ptrval = NULL;
    float *ptrvaldou = NULL;
    float *ptrvaladd = NULL;

    // Execute FFT
    if(pginput_signal==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Input buffer pointer is null %d",__LINE__);
        return;
    }
    if(pgfft_output==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Output buffer pointer is null %d",__LINE__);
        return;
    }

    if(max9814_is_power_of_4(SAMPLE_COUNT))
    {
        dsps_fft4r_fc32(pginput_signal, SAMPLE_COUNT>>1);
        dsps_bit_rev4r_fc32(pginput_signal, SAMPLE_COUNT>>1);
    }
    else
    {
        dsps_fft2r_fc32(pginput_signal, SAMPLE_COUNT>>1);
        dsps_bit_rev2r_fc32(pginput_signal, SAMPLE_COUNT>>1);
    }

    dsps_cplx2real_fc32(pginput_signal, SAMPLE_COUNT>>1);
    
    ptrval = pgfft_output;
    ptrvaldou = pginput_signal;
    ptrvaladd = pginput_signal+1;
    // Calculate spectrum amplitude (only take the first half
    for (int i = 0; i < SAMPLE_COUNT / 2; i++)
    {    
        *ptrval = 10 * log10f(((*ptrvaldou) * (*ptrvaldou) + (*ptrvaladd) * (*ptrvaladd) + 0.0000001) / SAMPLE_COUNT);
        //*ptrval = sqrt((*ptrvaldou)*(*ptrvaldou)+(*ptrvaladd)*(*ptrvaladd));
        ptrvaldou=ptrvaldou+2;
        ptrvaladd=ptrvaladd+2;
        ptrval++;
    }
}

// Calculate FFT
void max9814_compute_fft() 
{
    float *ptrval = NULL;
    float *ptrvaldou = NULL;
    float *ptrvaladd = NULL;

    // Execute FFT
    if(pginput_signal==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Input buffer pointer is null %d",__LINE__);
        return;
    }
    if(pgfft_output==NULL)
    {
        syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Output buffer pointer is null %d",__LINE__);
        return;
    }
    
    dsps_fft2r_fc32(pginput_signal, SAMPLE_COUNT);
    dsps_bit_rev_fc32(pginput_signal, SAMPLE_COUNT);
    dsps_cplx2reC_fc32(pginput_signal, SAMPLE_COUNT);
    
    ptrval = pgfft_output;
    ptrvaldou = pginput_signal;
    ptrvaladd = pginput_signal+1;

    // Calculate spectrum amplitude (only take the first half
    for (int i = 0; i < SAMPLE_COUNT / 2; i++)
    {    
        *ptrval = sqrt((*ptrvaldou)*(*ptrvaldou)+(*ptrvaladd)*(*ptrvaladd));
        ptrvaldou=ptrvaldou+2;
        ptrvaladd=ptrvaladd+2;
        ptrval++;
    }
}

// Find the maxnum power of frequency 
float max9814_find_frequency_of_maxpwr() 
{
    int peakIndex = 0;
    float peakValue = 0;
    float *ptrval = NULL;
    ptrval = (pgfft_output);

    for (int i = 10; i < SAMPLE_COUNT / 2; i++) 
    {
        if (*(pgfft_output+i) > peakValue) 
        {
            peakValue = *ptrval;
            peakIndex = i;
        }
        ptrval++;
    }

    float peakFreq = (peakIndex * SAMPLE_RATE) / SAMPLE_COUNT;
    return peakFreq;
}

// Calculate the power of frequency
float max9814_find_sumpwr_of_frequency(int minfreq, int maxfreq) 
{
    float peakValue = 0;
    int i = 0, looplimite = 0;

    looplimite = ((maxfreq * SAMPLE_COUNT)/SAMPLE_RATE)>(SAMPLE_RATE/2)?(SAMPLE_RATE/2):((maxfreq * SAMPLE_COUNT)/SAMPLE_RATE);

    for (i = (minfreq * SAMPLE_COUNT)/SAMPLE_RATE; i < looplimite; i++) 
    {
        peakValue = peakValue + (*(pgfft_output+i));
    }
    return peakValue;
}

void max9814_setup() 
{
    // Setup ADC
    ESP_ERROR_CHECK(max9814_adc_init());

    // Init FFT
#ifdef FFT4REAL    
    max9814_init_fft4real();
#else
    max9814_init_fft();
#endif
    return;
}

bool max9814_buildup4real()
{
    if(pginput_signal==NULL)
    {
        pginput_signal = (float *)malloc(SAMPLE_COUNT*sizeof(float));

        if(pginput_signal==NULL)
        {
            /* Memory fail */
            syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Input buffer pointer is null %d",__LINE__);
            return false;
        }
    }

    if(pgfft_output==NULL)
    {
        pgfft_output = (float *)malloc((SAMPLE_COUNT/2)*sizeof(float));
        if(pgfft_output==NULL)
        {
            /* Memory fail */
            free(pginput_signal);
            pginput_signal = NULL;
            syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Output buffer pointer is null %d",__LINE__);
            return false;
        }
    }

    if(max9814_sample_timer_handle==NULL)
    {
        esp_timer_create(&max9814_sample_timer4real_args, &max9814_sample_timer_handle);
    }
    else
    {
        esp_timer_stop(max9814_sample_timer_handle);
    }
    
    memset(pginput_signal,0,SAMPLE_COUNT*sizeof(float));
    memset(pgfft_output,0,(SAMPLE_COUNT/2)*sizeof(float));
    sample_index = 0;

    esp_timer_start_periodic(max9814_sample_timer_handle, 1000000 / SAMPLE_RATE); // Take SAMPLE_RATE samples within 1 second

    return true;
}

bool max9814_buildup()
{
    if(pginput_signal==NULL)
    {
        pginput_signal = (float *)malloc(SAMPLE_COUNT*2*sizeof(float));

        if(pginput_signal==NULL)
        {
            /* Memory fail */
            syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Input buffer pointer is null %d",__LINE__);
            return false;
        }
    }

    if(pgfft_output==NULL)
    {
        pgfft_output = (float *)malloc((SAMPLE_COUNT/2)*sizeof(float));
        if(pgfft_output==NULL)
        {
            /* Memory fail */
            free(pginput_signal);
            pginput_signal = NULL;
            syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_ERROR,"Output buffer pointer is null %d",__LINE__);
            return false;
        }
    }

    if(max9814_sample_timer_handle==NULL)
    {
        esp_timer_create(&max9814_sample_timer_args, &max9814_sample_timer_handle);
    }
    else
    {
        esp_timer_stop(max9814_sample_timer_handle);
    }
    
    memset(pginput_signal,0,2*SAMPLE_COUNT*sizeof(float));
    memset(pgfft_output,0,(SAMPLE_COUNT/2)*sizeof(float));
    sample_index = 0;

    esp_timer_start_periodic(max9814_sample_timer_handle, 1000000 / SAMPLE_RATE); // Take SAMPLE_RATE samples within 1 second

    return true;
}

void max9814_teardown() 
{
    if(pginput_signal)
    {
        free(pginput_signal);
        pginput_signal = NULL;
    }
    if(pgfft_output)
    {
        free(pgfft_output);
        pgfft_output = NULL;
    }
    return;
}

void max9814_check_bee(int checkfreq, int check2ndfreq, int *pwr1st, int *pwr2nd) 
{
    if(checkfreq>SAMPLE_RATE||check2ndfreq>SAMPLE_RATE)
    {
        return;
    }
#ifdef FFT4REAL 
    if(max9814_buildup4real())
#else
    if(max9814_buildup())
#endif
    {
        do
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
#ifdef FFT4REAL        
        while(sample_index < SAMPLE_COUNT);
#else
        while(sample_index < SAMPLE_COUNT*2);
#endif
        esp_timer_stop(max9814_sample_timer_handle);
#ifdef FFT4REAL
        max9814_compute_fft4real();
#else        
        max9814_compute_fft();
#endif
        *pwr1st = (int)max9814_find_sumpwr_of_frequency(checkfreq-MAX9814_BEE_FREQ_RANGE,checkfreq+MAX9814_BEE_FREQ_RANGE);
        if(check2ndfreq && pwr2nd!=NULL)
        {
            *pwr2nd = (int)max9814_find_sumpwr_of_frequency(check2ndfreq-MAX9814_BEE_FREQ_RANGE,check2ndfreq+MAX9814_BEE_FREQ_RANGE);
        }
    }
    max9814_teardown();
}

void max9814_display_signal(float *signal, int maxnumber, int step)
{
    int i = 0;
    for(i=0;i<maxnumber;i=i+step)
    {
        syslog_handler(SYSLOG_FACILITY_MAX9814,SYSLOG_LEVEL_DEBUG,"[%d] %f",i,*(signal+i));
    }
    return;
}
