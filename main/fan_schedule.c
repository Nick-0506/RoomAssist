/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fan_schedule.h"
#include "rmt.h"
#include "syslog.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_timer.h"

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

typedef enum {
    FS_STATE_IDLE,        // Waiting for first "fan off" event
    FS_STATE_COUNTDOWN,   // Counting down X minutes
    FS_STATE_VENTILATING, // Scheduled exhaust running for Y minutes
    FS_STATE_INTERRUPTED, // Countdown cancelled; waiting for all fans to clear
} fan_sched_state_t;

static fan_sched_state_t s_state     = FS_STATE_IDLE;
static uint32_t          s_interval_min = FAN_SCHEDULE_DEFAULT_INTERVAL_MIN;
static uint32_t          s_run_min      = FAN_SCHEDULE_DEFAULT_RUN_MIN;
static bool              s_enabled      = false;

static esp_timer_handle_t s_countdown_timer = NULL;
static esp_timer_handle_t s_run_timer       = NULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns true if any mode with higher preemption priority than SCHEDULE is
// currently active or pending in the Delta fan scheduler (i.e. modes 0–4).
static bool any_other_fan_active(void)
{
    uint8_t scheduler;
    for (int i = 0; i < IR_DELTA_FAN_TIGGER_MODE_SCHEDULE; i++)
    {
        ir_get_deltascheduler(i, &scheduler);
        if (scheduler > DELTA_FAN_SCHDULER_IDEL) return true;
    }
    return false;
}

static void start_countdown(void)
{
    s_state = FS_STATE_COUNTDOWN;
    esp_timer_start_once(s_countdown_timer,
                         (uint64_t)s_interval_min * 60ULL * 1000000ULL);
    syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                   "FanSchedule: Countdown started (%lu min)", s_interval_min);
}

// ---------------------------------------------------------------------------
// Timer callbacks
// ---------------------------------------------------------------------------

static void run_timer_cb(void *arg);

static void countdown_timer_cb(void *arg)
{
    syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                   "FanSchedule: Starting ventilation (%lu min)", s_run_min);
    s_state = FS_STATE_VENTILATING;
    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_SCHEDULE,
                       IR_DELTA_FAN_TIGGER_ACTIVE_ON,
                       IR_DELTA_FAN_DURATION_FOREVER);
    esp_timer_start_once(s_run_timer,
                         (uint64_t)s_run_min * 60ULL * 1000000ULL);
}

static void run_timer_cb(void *arg)
{
    syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                   "FanSchedule: Ventilation complete, restarting countdown");
    ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_SCHEDULE,
                       IR_DELTA_FAN_TIGGER_ACTIVE_OFF,
                       IR_DELTA_FAN_DURATION_FOREVER);
    // After each ventilation cycle, restart the X-minute countdown.
    start_countdown();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t fan_schedule_init(void)
{
    nvs_handle_t handle;
    if (nvs_open(FAN_SCHEDULE_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK)
    {
        uint32_t v;
        uint8_t  b;
        if (nvs_get_u32(handle, FAN_SCHEDULE_NVS_INTERVAL, &v) == ESP_OK)
            s_interval_min = v;
        if (nvs_get_u32(handle, FAN_SCHEDULE_NVS_RUN, &v) == ESP_OK)
            s_run_min = v;
        if (nvs_get_u8(handle, FAN_SCHEDULE_NVS_ENABLED, &b) == ESP_OK)
            s_enabled = (bool)b;
        nvs_close(handle);
        syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                       "FanSchedule: Loaded config interval=%lum run=%lum enabled=%d",
                       s_interval_min, s_run_min, (int)s_enabled);
    }

    esp_timer_create_args_t args = {
        .callback = countdown_timer_cb,
        .name     = "fs_countdown",
    };
    esp_err_t ret = esp_timer_create(&args, &s_countdown_timer);
    if (ret != ESP_OK) return ret;

    args.callback = run_timer_cb;
    args.name     = "fs_run";
    return esp_timer_create(&args, &s_run_timer);
}

void fan_schedule_set_config(uint32_t interval_min, uint32_t run_min,
                             bool enabled)
{
    bool changed = (s_interval_min != interval_min ||
                    s_run_min != run_min ||
                    s_enabled != enabled);

    s_interval_min = interval_min;
    s_run_min      = run_min;
    s_enabled      = enabled;

    if (changed)
    {
        // Reset state machine and cancel running timers on any config change.
        esp_timer_stop(s_countdown_timer);
        esp_timer_stop(s_run_timer);
        if (s_state == FS_STATE_VENTILATING)
        {
            ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_SCHEDULE,
                               IR_DELTA_FAN_TIGGER_ACTIVE_OFF,
                               IR_DELTA_FAN_DURATION_FOREVER);
        }
        s_state = FS_STATE_IDLE;

        nvs_handle_t handle;
        if (nvs_open(FAN_SCHEDULE_NVS_NAMESPACE, NVS_READWRITE,
                     &handle) == ESP_OK)
        {
            nvs_set_u32(handle, FAN_SCHEDULE_NVS_INTERVAL, interval_min);
            nvs_set_u32(handle, FAN_SCHEDULE_NVS_RUN, run_min);
            nvs_set_u8(handle, FAN_SCHEDULE_NVS_ENABLED, (uint8_t)enabled);
            nvs_commit(handle);
            nvs_close(handle);
        }
        syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                       "FanSchedule: Config updated interval=%lum run=%lum enabled=%d",
                       interval_min, run_min, (int)enabled);
    }
}

void fan_schedule_get_config(uint32_t *interval_min, uint32_t *run_min,
                             bool *enabled)
{
    if (interval_min) *interval_min = s_interval_min;
    if (run_min)      *run_min      = s_run_min;
    if (enabled)      *enabled      = s_enabled;
}

void fan_schedule_tick(void)
{
    // If feature is disabled or timings not set, ensure we stay/return to IDLE.
    if (!s_enabled || s_interval_min == 0 || s_run_min == 0)
    {
        if (s_state != FS_STATE_IDLE)
        {
            esp_timer_stop(s_countdown_timer);
            esp_timer_stop(s_run_timer);
            if (s_state == FS_STATE_VENTILATING)
            {
                ir_deltafan_tigger(IR_DELTA_FAN_TIGGER_MODE_SCHEDULE,
                                   IR_DELTA_FAN_TIGGER_ACTIVE_OFF,
                                   IR_DELTA_FAN_DURATION_FOREVER);
            }
            s_state = FS_STATE_IDLE;
        }
        return;
    }

    bool other_active = any_other_fan_active();

    switch (s_state)
    {
        case FS_STATE_IDLE:
            // Start countdown as soon as no other fan is running.
            if (!other_active) start_countdown();
            break;

        case FS_STATE_COUNTDOWN:
            // Req 4: any fan activation (humidity / smoke / manual) cancels countdown.
            if (other_active)
            {
                esp_timer_stop(s_countdown_timer);
                s_state = FS_STATE_INTERRUPTED;
                syslog_handler(SYSLOG_FACILITY_AIRQUALITY, SYSLOG_LEVEL_INFO,
                               "FanSchedule: Countdown interrupted");
            }
            break;

        case FS_STATE_VENTILATING:
            // Req 3: higher-priority modes (DRY/EXHAUST) preempt SCHEDULE via
            // the Delta fan scheduler automatically; no action needed here.
            // The run_timer_cb handles the Y-minute expiry.
            break;

        case FS_STATE_INTERRUPTED:
            // Req 4: restart countdown once all external fans have cleared.
            if (!other_active) start_countdown();
            break;
    }
}
