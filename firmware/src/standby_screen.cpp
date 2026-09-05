#include "standby_screen.h"

#include "standby_prefs.h"
#include "standby_quotes.h"

#include <M5GFX.h>
#include <cstring>
#include <time.h>

#include "lilygo_t5_display.h"

using lilygo_t5::flushDisplay;

namespace {

constexpr int kMargin = 16;
constexpr int kQuoteBandH = 130;
constexpr int kActionBoxW = 620;
constexpr int kActionBoxH = 200;

void drawBoldAscii(lilygo_t5::LilyGoT5Display &display, const char *text, int cx, int cy,
                   float scale) {
    display.setFont(&fonts::AsciiFont8x16);
    display.setTextSize(scale, scale);
    display.setTextDatum(textdatum_t::middle_center);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(text, cx, cy);
    display.drawString(text, cx, cy);
    display.setTextSize(1.0f, 1.0f);
}

void drawActionBox(lilygo_t5::LilyGoT5Display &display, int bx, int by, int box_w, int box_h,
                   const char *primary, float primary_scale, const char *secondary = nullptr) {
    display.drawRect(bx, by, box_w, box_h, TFT_BLACK);
    display.drawRect(bx + 4, by + 4, box_w - 8, box_h - 8, TFT_BLACK);
    display.fillRect(bx + 8, by + 8, box_w - 16, box_h - 16, TFT_WHITE);

    const int cx = bx + box_w / 2;
    const int cy = by + box_h / 2;

    if (secondary && secondary[0]) {
        drawBoldAscii(display, primary, cx, cy - 28, primary_scale);
        display.setFont(&fonts::Font4);
        display.setTextDatum(textdatum_t::middle_center);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.drawString(secondary, cx, cy + 36);
        return;
    }

    drawBoldAscii(display, primary, cx, cy, primary_scale);
}

int textWidth(lilygo_t5::LilyGoT5Display &display, const char *text) {
    return display.textWidth(text);
}

void drawWrappedQuote(lilygo_t5::LilyGoT5Display &display, const char *text, int x,
                      int y, int max_w, int line_h, int max_lines) {
    display.setFont(&fonts::Font2);
    display.setTextDatum(textdatum_t::top_center);
    display.setTextColor(TFT_BLACK, TFT_WHITE);

    char line[80];
    size_t line_len = 0;
    int lines = 0;
    const char *word_start = text;

    auto flush_line = [&]() {
        if (line_len == 0) return;
        line[line_len] = '\0';
        display.drawString(line, x, y + lines * line_h);
        lines++;
        line_len = 0;
    };

    while (*word_start && lines < max_lines) {
        while (*word_start == ' ') word_start++;
        if (!*word_start) break;

        const char *word_end = word_start;
        while (*word_end && *word_end != ' ') word_end++;

        char word[48];
        size_t word_len = static_cast<size_t>(word_end - word_start);
        if (word_len >= sizeof(word)) word_len = sizeof(word) - 1;
        memcpy(word, word_start, word_len);
        word[word_len] = '\0';

        char trial[80];
        if (line_len == 0) {
            snprintf(trial, sizeof(trial), "%s", word);
        } else {
            snprintf(trial, sizeof(trial), "%s %s", line, word);
        }

        if (textWidth(display, trial) <= max_w) {
            if (line_len > 0) line[line_len++] = ' ';
            memcpy(line + line_len, word, word_len + 1);
            line_len += word_len;
        } else {
            flush_line();
            if (lines >= max_lines) break;
            strncpy(line, word, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            line_len = strlen(line);
        }
        word_start = word_end;
    }
    flush_line();
}

void drawLoadingPanel(lilygo_t5::LilyGoT5Display &display) {
    const int w = display.width();
    const int h = display.height();
    const int bx = (w - kActionBoxW) / 2;
    const int by = (h - kActionBoxH) / 2;
    drawActionBox(display, bx, by, kActionBoxW, kActionBoxH, "Loading...", 4.0f,
                  "Connecting & fetching trains");
}

void drawTapButton(lilygo_t5::LilyGoT5Display &display) {
    const int w = display.width();
    const int h = display.height();
    const int bx = (w - kActionBoxW) / 2;
    const int by = (h - kQuoteBandH - kActionBoxH) / 2;
    drawActionBox(display, bx, by, kActionBoxW, kActionBoxH, "PRESS RST", 3.5f, "to check trains");
}

void drawQuoteFooter(lilygo_t5::LilyGoT5Display &display, time_t last_checked_epoch) {
    const int w = display.width();
    const int h = display.height();
    const int band_y = h - kQuoteBandH;

    display.fillRect(0, band_y - 6, w, kQuoteBandH + 6, TFT_WHITE);
    display.drawFastHLine(kMargin, band_y - 6, w - kMargin * 2, TFT_BLACK);

    char checked[48];
    standby_prefs::formatLastCheckedLine(last_checked_epoch, checked, sizeof(checked));
    display.setFont(&fonts::Font4);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(checked, w / 2, band_y + kQuoteBandH / 2);
}

void drawBattery(lilygo_t5::LilyGoT5Display &display, int battery_percent) {
    if (battery_percent < 0 || battery_percent > 100) return;
    
    const int right_x = display.width() - kMargin;
    const int top_y = kMargin;
    const int gauge_w = 80;
    const int gauge_h = 20;
    const int gauge_x = right_x - gauge_w;
    
    // Draw "Battery" label
    display.setFont(&fonts::Font2);
    display.setTextDatum(textdatum_t::top_right);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString("Battery", gauge_x - 8, top_y);
    
    // Draw battery gauge
    display.drawRect(gauge_x, top_y, gauge_w, gauge_h, TFT_BLACK);
    
    // Fill gauge based on percentage
    if (battery_percent > 0) {
        const int fill_w = (gauge_w - 4) * battery_percent / 100;
        display.fillRect(gauge_x + 2, top_y + 2, fill_w, gauge_h - 4, TFT_BLACK);
    }
    
    // Draw percentage text
    char batt[16];
    snprintf(batt, sizeof(batt), "%d%%", battery_percent);
    display.setTextDatum(textdatum_t::top_left);
    display.drawString(batt, gauge_x + gauge_w + 8, top_y);
}

}  // namespace

namespace standby_screen {

void render(lilygo_t5::LilyGoT5Display &display, int battery_percent,
            time_t last_checked_epoch, size_t quote_index) {
    display.startWrite();
    display.fillScreen(TFT_WHITE);
    display.setTextWrap(false);
    drawBattery(display, battery_percent);
    drawTapButton(display);
    drawQuoteFooter(display, last_checked_epoch);
    display.endWrite();
    flushDisplay(display);
}

void renderStatic(lilygo_t5::LilyGoT5Display &display, int battery_percent,
                  time_t last_checked_epoch, size_t quote_index) {
    // Static screen is the same as regular standby screen
    render(display, battery_percent, last_checked_epoch, quote_index);
}

void renderLoading(lilygo_t5::LilyGoT5Display &display, int battery_percent) {
    display.startWrite();
    display.fillScreen(TFT_WHITE);
    display.setTextWrap(false);
    drawBattery(display, battery_percent);
    drawLoadingPanel(display);
    display.endWrite();
    flushDisplay(display);
}

}  // namespace standby_screen
