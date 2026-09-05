#pragma once

#include <Arduino.h>
#include <esp_sleep.h>

#include "input_controls.h"

inline void disconnectWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// ESP32-S3 native USB (usbmodem) drops off the host during light sleep — the port
// vanishes shortly after boot once standby starts sleeping. Skip sleep while a USB
// host is connected so flashing and serial monitoring stay stable.
inline bool usbHostConnected() {
#if ARDUINO_USB_CDC_ON_BOOT
    return static_cast<bool>(Serial);
#else
    return false;
#endif
}

// Light sleep in small chunks; returns true if the screen was tapped.
inline bool lightSleepWaitTouch(uint32_t ms) {
    const uint32_t chunk_ms = 500;
    uint32_t remaining = ms;
    while (remaining > 0) {
        if (mbta_input::pollScreenTap()) return true;
        const uint32_t nap = remaining > chunk_ms ? chunk_ms : remaining;
        if (usbHostConnected()) {
            delay(nap);
        } else {
            esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(nap) * 1000ULL);
            esp_light_sleep_start();
        }
        remaining -= nap;
    }
    return false;
}

// Light sleep without touch polling (active train screen — taps ignored).
inline void lightSleepOnly(uint32_t ms) {
    const uint32_t chunk_ms = 500;
    uint32_t remaining = ms;
    while (remaining > 0) {
        const uint32_t nap = remaining > chunk_ms ? chunk_ms : remaining;
        if (usbHostConnected()) {
            delay(nap);
        } else {
            esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(nap) * 1000ULL);
            esp_light_sleep_start();
        }
        remaining -= nap;
    }
}

inline void lightSleepWait(uint32_t ms) {
    (void)lightSleepWaitTouch(ms);
}
