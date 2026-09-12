#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <time.h>
#include <math.h>
#include <esp_sleep.h>

#include "bq27220.h"
#include "lilygo_t5_display.h"
#include "mbta_constants.h"
#include "predictions.h"
#include "secrets.h"
#include "standby_prefs.h"
#include "standby_quotes.h"
#include "standby_screen.h"

using lilygo_t5::LilyGoT5Display;
using lilygo_t5::flushDisplay;

static LilyGoT5Display display;
static BQ27220 g_battery;
static bool g_battery_ready = false;

static constexpr const char *STOP_NAME = "Summit Avenue";
static constexpr const char *DESTINATION = "Government Center";
static constexpr int kMargin = 16;
static constexpr int kWeatherInvalid = -1;
static constexpr int kBatteryInvalid = -1;

struct ScreenData {
    TrainEstimate next;
    TrainEstimate then;
    int weather_code = kWeatherInvalid;
    int humidity_percent = kWeatherInvalid;
    int temp_c = kWeatherInvalid;
    int uv_index = kWeatherInvalid;
    String status_message;
    int battery_percent = kBatteryInvalid;
    bool battery_charging = false;
    String wifi_line;
    String updated_line;
};

struct WeatherSnapshot {
    int weather_code = kWeatherInvalid;
    int precip_percent = 0;
    int humidity_percent = kWeatherInvalid;
    int temp_c = kWeatherInvalid;
    int uv_index = kWeatherInvalid;
};

static WeatherSnapshot g_weather;
static ScreenData g_last_screen;
static bool g_has_last_screen = false;
static time_t g_last_checked_epoch = 0;
static uint16_t g_quote_index = 0;

static bool waitForValidTime();
static String wifiLine();
static void renderScreen(const ScreenData &screen);
static void renderStaticScreen();
static String buildFooterLine(const String &base);
static void renderCachedNormal();
static void refreshAllCaches(const char *status_override);
static void runActiveSession();

static void wifiPowerDown() {
    WiFi.disconnect(true, true);  // More aggressive disconnect
    WiFi.mode(WIFI_OFF);
    delay(100);  // Allow time for WiFi radio to fully power down
}

static void wifiPowerUp() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
}

static bool connectWifi() {
    if (WiFi.status() == WL_CONNECTED) {
        predictionsEnsureTime();
        return true;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Reduce WiFi connection attempts from 60 to 30 for faster startup
    for (int i = 0; i < 30; ++i) {
        if (WiFi.status() == WL_CONNECTED) {
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
            tzset();
            waitForValidTime();
            return true;
        }
        delay(250);
    }
    return false;
}

static void applyCachedTrains(ScreenData &screen) {
    if (!g_has_last_screen) return;
    screen.next = g_last_screen.next;
    screen.then = g_last_screen.then;
    if (screen.next.arrival_epoch > 0) {
        screen.status_message = "";
    }
}

static void finalizeScreenFooter(ScreenData &screen, const char *status_override) {
    if (status_override) {
        screen.updated_line = status_override;
    } else if (screen.next.minutes < 0) {
        screen.updated_line = "No upcoming trains";
    } else {
        screen.updated_line = updatedLine();
    }
    screen.updated_line = buildFooterLine(screen.updated_line);
}

static constexpr int kTopPad = 100;
static constexpr int kBottomPad = 20;
static constexpr int kSectionGap = 12;
static constexpr int kBannerH = 56;
static constexpr int kRouteH = 56;
static constexpr int kWeatherH = 60;
static constexpr int kStatusH = 48;
static constexpr int kFooterH = 44;
static constexpr int kFixedH =
    kTopPad + kBannerH + kRouteH + kWeatherH + (kStatusH * 2) + kFooterH + kBottomPad +
    (6 * (1 + kSectionGap));

static void initBattery() {
    Wire.begin(39, 40);
    Wire.setClock(400000);
    g_battery_ready = g_battery.init();
}

static void readBattery(int &percent_out, bool &charging_out) {
    if (!g_battery_ready) {
        percent_out = kBatteryInvalid;
        charging_out = false;
        return;
    }
    percent_out = g_battery.getStateOfCharge();
    if (percent_out > 100) percent_out = 100;
    charging_out = g_battery.getIsCharging();
}

static void drawCloudBlob(int cx, int cy, int scale) {
    display.fillCircle(cx - scale, cy, scale, TFT_BLACK);
    display.fillCircle(cx + scale / 3, cy - scale / 2, scale, TFT_BLACK);
    display.fillCircle(cx + scale, cy, scale - 1, TFT_BLACK);
    display.fillRect(cx - scale - 1, cy, scale * 2 + scale / 3 + 2, scale + 1, TFT_BLACK);
}

static void drawWeatherIcon(int cx, int cy, int code, int size) {
    const int r = size / 4;
    if (code == 0) {
        display.fillCircle(cx, cy, r, TFT_BLACK);
        for (int i = 0; i < 8; ++i) {
            const float a = i * (float)M_PI / 4.0f;
            const int x1 = cx + (int)lround(cosf(a) * (r + 2));
            const int y1 = cy + (int)lround(sinf(a) * (r + 2));
            const int x2 = cx + (int)lround(cosf(a) * (r + size / 3));
            const int y2 = cy + (int)lround(sinf(a) * (r + size / 3));
            display.drawLine(x1, y1, x2, y2, TFT_BLACK);
        }
        return;
    }
    if (code >= 1 && code <= 2) {
        display.fillCircle(cx - size / 5, cy - size / 8, r - 1, TFT_BLACK);
        for (int i = 0; i < 8; ++i) {
            const float a = i * (float)M_PI / 4.0f;
            const int x2 = cx - size / 5 + (int)lround(cosf(a) * (r + size / 5));
            const int y2 = cy - size / 8 + (int)lround(sinf(a) * (r + size / 5));
            display.drawLine(cx - size / 5, cy - size / 8, x2, y2, TFT_BLACK);
        }
        drawCloudBlob(cx + size / 8, cy + size / 10, size / 5);
        return;
    }
    if (code == 3) {
        drawCloudBlob(cx, cy, size / 4);
        return;
    }
    if (code == 45 || code == 48) {
        for (int i = -2; i <= 2; ++i) {
            display.drawLine(cx - size / 2, cy + i * 3, cx + size / 2, cy + i * 3, TFT_BLACK);
        }
        return;
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        drawCloudBlob(cx, cy - size / 8, size / 5);
        for (int i = -1; i <= 1; ++i) {
            display.drawLine(cx + i * (size / 4), cy + size / 4, cx + i * (size / 4) - 2,
                             cy + size / 2, TFT_BLACK);
        }
        return;
    }
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
        drawCloudBlob(cx, cy - size / 8, size / 5);
        display.drawPixel(cx - 3, cy + size / 3, TFT_BLACK);
        display.drawPixel(cx, cy + size / 3 + 2, TFT_BLACK);
        display.drawPixel(cx + 3, cy + size / 3, TFT_BLACK);
        display.drawPixel(cx - 2, cy + size / 3 + 4, TFT_BLACK);
        display.drawPixel(cx + 2, cy + size / 3 + 4, TFT_BLACK);
        return;
    }
    if (code >= 95 && code <= 99) {
        drawCloudBlob(cx, cy - size / 6, size / 5);
        display.drawLine(cx - 4, cy + size / 4, cx + 2, cy + size / 2, TFT_BLACK);
        display.drawLine(cx + 4, cy + size / 4, cx - 2, cy + size / 2, TFT_BLACK);
        return;
    }
    display.drawRect(cx - size / 4, cy - size / 4, size / 2, size / 2, TFT_BLACK);
}

static void drawRule(int y, int width) {
    const int x = (display.width() - width) / 2;
    display.drawFastHLine(x, y, width, TFT_BLACK);
}

static void drawSectionCenter(const char *text, int section_y, int section_h, const lgfx::IFont *font) {
    display.fillRect(kMargin, section_y, display.width() - kMargin * 2, section_h, TFT_WHITE);
    display.setFont(font);
    display.setTextDatum(textdatum_t::middle_center);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(text, display.width() / 2, section_y + section_h / 2);
}

static void drawBigText(int cx, int cy, int clear_w, int clear_h, const char *text,
                          const lgfx::IFont *font) {
    display.fillRect(cx - clear_w / 2, cy - clear_h / 2, clear_w, clear_h, TFT_WHITE);
    display.setFont(font);
    display.setTextDatum(textdatum_t::middle_center);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(text, cx, cy);
}

static void drawBanner(const char *text, int section_y, int section_h) {
    const int cx = display.width() / 2;
    const int cy = section_y + section_h / 2;
    const int clear_w = display.width() - kMargin * 2;

    display.fillRect(kMargin, section_y, clear_w, section_h, TFT_WHITE);
    display.setFont(&fonts::AsciiFont8x16);
    display.setTextSize(2.0f, 2.0f);
    display.setTextDatum(textdatum_t::middle_center);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    // Remove duplicate drawString call - was drawing same text twice
    display.drawString(text, cx, cy);
    display.setTextSize(1.0f, 1.0f);
}

static void drawBigNumber(int cx, int cy, int clear_w, int clear_h, int value,
                          const lgfx::IFont *font) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    drawBigText(cx, cy, clear_w, clear_h, buf, font);
}

static int valueColumnX() { return display.width() / 2; }

static void drawRowLabel(const char *label, int mid_y) {
    display.setFont(&fonts::Font4);
    display.setTextDatum(textdatum_t::middle_left);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(label, kMargin, mid_y);
}

static void drawRowValueText(const char *text, int mid_y) {
    display.setFont(&fonts::Font4);
    display.setTextDatum(textdatum_t::middle_left);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(text, valueColumnX(), mid_y);
}

static void drawPanel(int x, int y, int w, int h, const char *label, const TrainEstimate &train) {
    display.drawRect(x, y, w, h, TFT_BLACK);
    display.drawFastHLine(x + 12, y + 36, w - 24, TFT_BLACK);

    display.setFont(&fonts::Font4);
    display.setTextDatum(textdatum_t::top_center);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(label, x + w / 2, y + 8);

    if (train.minutes >= 0) {
        const int mid_y = y + (h * 42) / 100;
        drawBigNumber(x + w / 2, mid_y, w - 12, 110, train.minutes, &fonts::Font8);
        const char *source = predSourceLabel(train.source);
        if (source && source[0]) {
            display.setFont(&fonts::Font2);
            display.setTextDatum(textdatum_t::top_center);
            display.drawString(source, x + w / 2, y + (h * 52) / 100);
        }
        display.setFont(&fonts::Font4);
        display.setTextDatum(textdatum_t::top_center);
        display.drawString("minutes", x + w / 2, y + (h * 58) / 100);
        display.drawString(train.clock_time.c_str(), x + w / 2, y + (h * 72) / 100);
    } else {
        display.fillRect(x + 4, y + 38, w - 8, h - 42, TFT_WHITE);
        display.setFont(&fonts::Font4);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString("No train", x + w / 2, y + h / 2);
    }
}

static void drawWeatherLine(int y, int section_h, const ScreenData &screen) {
    const int mid_y = y + section_h / 2;
    const int value_x = valueColumnX();
    const int right_x = display.width() - kMargin;

    if (screen.status_message.length() > 0) {
        drawRowLabel("Weather:", mid_y);
        drawRowValueText(screen.status_message.c_str(), mid_y);
        return;
    }
    if (screen.humidity_percent < 0 || screen.temp_c == kWeatherInvalid) {
        drawRowLabel("Weather:", mid_y);
        drawRowValueText("--", mid_y);
        return;
    }

    char values[40];
    if (screen.uv_index >= 0) {
        snprintf(values, sizeof(values), "%dC  %d%%  UV%d", screen.temp_c, screen.humidity_percent,
                 screen.uv_index);
    } else {
        snprintf(values, sizeof(values), "%dC  %d%%", screen.temp_c, screen.humidity_percent);
    }

    drawRowLabel("Weather:", mid_y);

    display.setFont(&fonts::Font4);
    const int icon_size = 24;
    const int gap = 8;
    const int text_w = display.textWidth(values);
    const int icon_x = value_x + icon_size / 2;
    const int text_x = value_x + icon_size + gap;

    if (text_x + text_w > right_x) {
        drawRowValueText(values, mid_y);
        return;
    }

    drawWeatherIcon(icon_x, mid_y, screen.weather_code, icon_size);
    display.setTextDatum(textdatum_t::middle_left);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(values, text_x, mid_y);
}

static void drawBatteryLine(int y, int section_h, int percent, bool charging) {
    const int mid_y = y + section_h / 2;
    const int value_x = valueColumnX();
    const int right_x = display.width() - kMargin;

    char value[28];
    if (percent >= 0) {
        if (charging) {
            snprintf(value, sizeof(value), "%d", percent);
            const size_t len = strlen(value);
            value[len] = '%';
            value[len + 1] = '\0';
            strncat(value, " charging", sizeof(value) - strlen(value) - 1);
        } else {
            snprintf(value, sizeof(value), "%d", percent);
            const size_t len = strlen(value);
            value[len] = '%';
            value[len + 1] = '\0';
        }
    } else {
        strncpy(value, "n/a", sizeof(value));
    }

    drawRowLabel("Battery:", mid_y);

    display.setFont(&fonts::Font4);
    const int value_w = display.textWidth(value);
    const int gap = 10;
    const int bar_h = 20;
    const int bar_y = mid_y - bar_h / 2;
    const int bar_x = value_x;
    const int bar_w = right_x - value_w - gap - bar_x;

    if (bar_w > 40 && percent >= 0) {
        display.drawRect(bar_x, bar_y, bar_w, bar_h, TFT_BLACK);
        const int fill_w = (bar_w - 4) * percent / 100;
        if (fill_w > 0) {
            display.fillRect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, TFT_BLACK);
        }
        display.setTextDatum(textdatum_t::middle_right);
        display.drawString(value, right_x, mid_y);
    } else {
        drawRowValueText(value, mid_y);
    }
}

static void drawWifiLine(int y, int section_h, const char *wifi_line) {
    const int mid_y = y + section_h / 2;
    drawRowLabel("WiFi:", mid_y);
    drawRowValueText(wifi_line, mid_y);
}

static String wifiLine() {
    return WiFi.status() == WL_CONNECTED ? String("connected") : String("offline");
}

static void drawSectionRule(int &y, int content_w) {
    drawRule(y, content_w);
    y += 1 + kSectionGap;
}

static void drawMbtaScreen(const ScreenData &screen) {
    const int h = display.height();
    const int content_w = display.width() - kMargin * 2;
    const int panel_h = h - kFixedH;
    const int panel_w = (content_w - 20) / 2;

    display.fillScreen(TFT_WHITE);
    display.setTextWrap(false);

    int y = kTopPad;

    drawBanner("GREEN C", y, kBannerH);
    y += kBannerH;
    drawSectionRule(y, content_w);

    char route_line[64];
    snprintf(route_line, sizeof(route_line), "%s -> %s", STOP_NAME, DESTINATION);
    drawSectionCenter(route_line, y, kRouteH, &fonts::Font4);
    y += kRouteH;
    drawSectionRule(y, content_w);

    drawPanel(kMargin, y, panel_w, panel_h, "NEXT", screen.next);
    drawPanel(kMargin + panel_w + 20, y, panel_w, panel_h, "THEN", screen.then);
    y += panel_h;
    drawSectionRule(y, content_w);

    drawWeatherLine(y, kWeatherH, screen);
    y += kWeatherH;
    drawSectionRule(y, content_w);

    drawBatteryLine(y, kStatusH, screen.battery_percent, screen.battery_charging);
    y += kStatusH;
    drawSectionRule(y, content_w);

    drawWifiLine(y, kStatusH, screen.wifi_line.c_str());
    y += kStatusH;
    drawSectionRule(y, content_w);

    if (screen.updated_line.length() > 0) {
        drawSectionCenter(screen.updated_line.c_str(), h - kBottomPad - kFooterH, kFooterH,
                          &fonts::Font4);
    }

    // Remove redundant banner draw - already drawn at start
    // drawBanner("GREEN C", kTopPad, kBannerH);
}

static void renderStaticScreen() {
    int battery_percent = kBatteryInvalid;
    bool charging = false;
    readBattery(battery_percent, charging);
    standby_screen::renderStatic(display, battery_percent, g_last_checked_epoch, g_quote_index);
}

static void runActiveSession() {
    g_quote_index = standby_prefs::advanceQuoteIndex(g_quote_index, kStandbyQuoteCount);

    int battery_percent = kBatteryInvalid;
    bool charging = false;
    readBattery(battery_percent, charging);
    standby_screen::renderLoading(display, battery_percent);

    wifiPowerUp();
    refreshAllCaches("Loading trains...");

    if (g_has_last_screen) {
        renderCachedNormal();
    } else {
        ScreenData loading;
        loading.status_message = "No trains";
        loading.wifi_line = wifiLine();
        readBattery(loading.battery_percent, loading.battery_charging);
        applyWeather(loading);
        finalizeScreenFooter(loading, nullptr);
        renderScreen(loading);
    }

    // Keep screen active for 90 seconds (no refresh needed - e-paper retains image)
    delay(kActiveTimeoutMs);

    // Save state and power down
    const time_t now = time(nullptr);
    if (now > 1700000000) {
        g_last_checked_epoch = now;
        standby_prefs::saveLastCheckedEpoch(now);
    }

    wifiPowerDown();
}

static String buildFooterLine(const String &base) {
    return base;
}

static void renderScreen(const ScreenData &screen) {
    display.startWrite();
    drawMbtaScreen(screen);
    display.endWrite();
    flushDisplay(display);
}

static bool waitForValidTime() {
    for (int i = 0; i < 30; ++i) {
        const time_t now = time(nullptr);
        if (now > 1700000000) return true;
        delay(500);
    }
    return false;
}

static bool fetchWeather(WeatherSnapshot &weather_out) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    const char *url =
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=42.3318&longitude=-71.1212"
        "&current=weather_code,relative_humidity_2m,temperature_2m,uv_index"
        "&hourly=precipitation_probability"
        "&timezone=America/New_York&forecast_hours=24";

    if (!http.begin(client, url)) return false;
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, http.getString())) {
        http.end();
        return false;
    }
    http.end();

    weather_out.weather_code = doc["current"]["weather_code"] | kWeatherInvalid;
    weather_out.humidity_percent = doc["current"]["relative_humidity_2m"] | kWeatherInvalid;
    weather_out.temp_c = (int)lround(doc["current"]["temperature_2m"] | (double)kWeatherInvalid);
    weather_out.uv_index = (int)lround(doc["current"]["uv_index"] | (double)kWeatherInvalid);
    weather_out.precip_percent = 0;

    JsonArray times = doc["hourly"]["time"].as<JsonArray>();
    JsonArray probs = doc["hourly"]["precipitation_probability"].as<JsonArray>();
    if (!times.isNull() && !probs.isNull() && times.size() > 0) {
        char hour_key[32];
        struct tm local_tm = {};
        const time_t now = time(nullptr);
        localtime_r(&now, &local_tm);
        strftime(hour_key, sizeof(hour_key), "%Y-%m-%dT%H:00", &local_tm);

        for (size_t i = 0; i < times.size(); ++i) {
            const char *stamp = times[i];
            if (stamp && strcmp(stamp, hour_key) == 0) {
                weather_out.precip_percent = probs[i] | 0;
                break;
            }
        }
    }

    return weather_out.humidity_percent >= 0 && weather_out.temp_c != kWeatherInvalid;
}

static String updatedLine() {
    time_t now = time(nullptr);
    struct tm local_tm = {};
    localtime_r(&now, &local_tm);
    char timebuf[16];
    strftime(timebuf, sizeof(timebuf), "%I:%M %p", &local_tm);
    char out[32];
    if (timebuf[0] == '0') {
        snprintf(out, sizeof(out), "Updated %s", timebuf + 1);
    } else {
        snprintf(out, sizeof(out), "Updated %s", timebuf);
    }
    return String(out);
}

static void applyWeather(ScreenData &screen) {
    screen.weather_code = g_weather.weather_code;
    screen.humidity_percent = g_weather.humidity_percent;
    screen.temp_c = g_weather.temp_c;
    screen.uv_index = g_weather.uv_index;
}

static ScreenData fetchScreenData(const char *status_override = nullptr) {
    ScreenData screen;
    readBattery(screen.battery_percent, screen.battery_charging);
    applyWeather(screen);
    screen.wifi_line = wifiLine();

    if (!connectWifi()) {
        screen.status_message = "No Internet";
        screen.wifi_line = "offline";
        applyCachedTrains(screen);
        finalizeScreenFooter(screen, status_override);
        return screen;
    }

    String error;
    if (!fetchSummitArrivals(screen.next, screen.then, error)) {
        screen.status_message = error;
        applyCachedTrains(screen);
    } else {
        screen.status_message = "";
    }

    WeatherSnapshot weather;
    if (fetchWeather(weather)) {
        g_weather = weather;
    }
    applyWeather(screen);

    screen.wifi_line = wifiLine();
    readBattery(screen.battery_percent, screen.battery_charging);
    return screen;
}

static void renderCachedNormal() {
    ScreenData render_data = g_last_screen;
    readBattery(render_data.battery_percent, render_data.battery_charging);
    applyWeather(render_data);
    render_data.wifi_line = wifiLine();
    refreshTrainEstimates(render_data.next, render_data.then);
    finalizeScreenFooter(render_data, nullptr);
    renderScreen(render_data);
}

static void refreshAllCaches(const char *status_override) {
    ScreenData render_data = fetchScreenData(status_override);

    if (render_data.next.arrival_epoch > 0) {
        refreshTrainEstimates(render_data.next, render_data.then);
        finalizeScreenFooter(render_data, status_override);
        g_last_screen = render_data;
        g_has_last_screen = true;
    } else if (g_has_last_screen) {
        g_last_screen.battery_percent = render_data.battery_percent;
        g_last_screen.battery_charging = render_data.battery_charging;
        g_last_screen.weather_code = render_data.weather_code;
        g_last_screen.humidity_percent = render_data.humidity_percent;
        g_last_screen.temp_c = render_data.temp_c;
        g_last_screen.uv_index = render_data.uv_index;
        g_last_screen.wifi_line = render_data.wifi_line;
        if (render_data.status_message.length() > 0) {
            g_last_screen.status_message = render_data.status_message;
        }
        refreshTrainEstimates(g_last_screen.next, g_last_screen.then);
        finalizeScreenFooter(g_last_screen, status_override);
    } else {
        finalizeScreenFooter(render_data, status_override);
        g_last_screen = render_data;
    }
}

void setup() {
    // Disable Serial to save power - USB serial interface consumes power even when not connected
    // Serial.begin(115200);
    // delay(500);

    if (!display.init_without_reset(false)) {
        while (true) delay(1000);
    }

    initBattery();
    predictionsResetCache();
    standby_prefs::loadLastCheckedEpoch(g_last_checked_epoch);
    standby_prefs::loadQuoteIndex(g_quote_index);

    wifiPowerDown();

    // Always run active session on boot, then show static screen
    runActiveSession();

    renderStaticScreen();
}

void loop() {
    // Enter deep sleep - will only wake up via RST button (hardware reset)
    // Serial.println("[MBTA] Entering deep sleep. Press RST to check trains.");
    delay(1000);
    
    // Power down I2C interface before deep sleep to prevent battery drain
    Wire.end();
    
    // Ensure display is in lowest power mode
    display.powerSaveOn();
    
    // Use deep sleep with no wake-up sources - only RST button will wake the device
    esp_deep_sleep_start();
}
