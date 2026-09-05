#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "TouchDrvGT911.hpp"

namespace mbta_input {

static constexpr int kPinBacklight = 11;
static constexpr int kPinTouchRst = 9;
static constexpr int kPinTouchInt = 3;
static constexpr int kI2cSda = 39;
static constexpr int kI2cScl = 40;
static constexpr uint32_t kHomeDebounceMs = 250;
static constexpr uint32_t kPollMs = 40;

static TouchDrvGT911 g_touch;
static bool g_touch_ready = false;
static bool g_backlight_on = false;
static uint32_t g_last_home_ms = 0;
static bool g_screen_touch_active = false;
static uint32_t g_last_screen_tap_ms = 0;
static constexpr uint32_t kScreenTapDebounceMs = 400;

inline void setBacklight(bool on) {
    pinMode(kPinBacklight, OUTPUT);
    g_backlight_on = on;
    analogWrite(kPinBacklight, on ? 230 : 0);
}

inline void toggleBacklight() {
    setBacklight(!g_backlight_on);
}

inline bool initTouch(int display_w, int display_h) {
    g_touch.setPins(kPinTouchRst, kPinTouchInt);
    if (!g_touch.begin(Wire, GT911_SLAVE_ADDRESS_L, kI2cSda, kI2cScl)) {
        return false;
    }

    g_touch.setHomeButtonCallback(
        [](void *) {
            const uint32_t now = millis();
            if (now - g_last_home_ms < kHomeDebounceMs) {
                return;
            }
            g_last_home_ms = now;
            toggleBacklight();
        },
        nullptr);

    g_touch.setInterruptMode(0x03);  // TouchDrvGT911::LOW_LEVEL_QUERY
    g_touch.setMaxCoordinates(display_w - 1, display_h - 1);
    g_touch_ready = true;
    return true;
}

inline bool pollTouchInput(bool *screen_tap_out = nullptr) {
    if (!g_touch_ready) {
        if (screen_tap_out) *screen_tap_out = false;
        return false;
    }

    int16_t x[1];
    int16_t y[1];
    const int count = g_touch.getPoint(x, y, 1);

    if (!screen_tap_out) return count > 0;

    if (count <= 0) {
        g_screen_touch_active = false;
        *screen_tap_out = false;
        return false;
    }

    if (g_screen_touch_active) {
        *screen_tap_out = false;
        return true;
    }
    g_screen_touch_active = true;

    const uint32_t now = millis();
    if (now - g_last_screen_tap_ms < kScreenTapDebounceMs) {
        *screen_tap_out = false;
        return true;
    }
    g_last_screen_tap_ms = now;
    *screen_tap_out = true;
    return true;
}

inline void pollTouch() { pollTouchInput(nullptr); }

inline bool pollScreenTap() {
    bool tapped = false;
    pollTouchInput(&tapped);
    return tapped;
}

// Screen tap is used on the standby home screen only (wake). HOME toggles backlight.
inline bool waitWithInput(uint32_t ms) {
    const uint32_t start = millis();
    while (millis() - start < ms) {
        if (pollScreenTap()) return true;
        delay(kPollMs);
    }
    return false;
}

}  // namespace mbta_input
