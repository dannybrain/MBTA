#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include <time.h>

#include "mbta_constants.h"

// Fixed local-time overnight window (America/New_York via firmware TZ).
// No MBTA schedule lookup required — saves WiFi and avoids missed pauses.

inline bool overnightPauseActive() {
    const time_t now = time(nullptr);
    if (now == (time_t)-1) return false;

    struct tm local_tm = {};
    localtime_r(&now, &local_tm);
    const int now_mins = local_tm.tm_hour * 60 + local_tm.tm_min;
    const int start_mins = kOvernightStartHour * 60 + kOvernightStartMinute;
    const int end_mins = kOvernightResumeHour * 60 + kOvernightResumeMinute;
    return now_mins >= start_mins && now_mins < end_mins;
}

inline uint32_t overnightMsUntilResume() {
    const time_t now = time(nullptr);
    if (now == (time_t)-1) return 0;

    struct tm local_tm = {};
    localtime_r(&now, &local_tm);

    struct tm resume_tm = local_tm;
    resume_tm.tm_hour = kOvernightResumeHour;
    resume_tm.tm_min = kOvernightResumeMinute;
    resume_tm.tm_sec = 0;

    time_t resume_epoch = mktime(&resume_tm);
    if (resume_epoch == (time_t)-1) return 0;

    if (difftime(resume_epoch, now) <= 0.0) {
        return 0;
    }
    return static_cast<uint32_t>(difftime(resume_epoch, now) * 1000.0);
}

// Single light-sleep until resume — no touch poll, no WiFi (caller must power down first).
inline void overnightSleepUntilResume() {
    const uint32_t ms = overnightMsUntilResume();
    if (ms == 0) return;
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(ms) * 1000ULL);
    esp_light_sleep_start();
}

inline void overnightResumeClockText(char *out, size_t out_len) {
    const int hour24 = kOvernightResumeHour;
    const int h12 = (hour24 % 12 == 0) ? 12 : (hour24 % 12);
    snprintf(out, out_len, "%d:%02d %s", h12, kOvernightResumeMinute,
             hour24 < 12 ? "AM" : "PM");
}
