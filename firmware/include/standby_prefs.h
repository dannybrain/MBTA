#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

namespace standby_prefs {

inline Preferences &store() {
    static Preferences prefs;
    return prefs;
}

inline void loadLastCheckedEpoch(time_t &epoch_out) {
    if (!store().begin("mbta_v3", true)) {
        epoch_out = 0;
        return;
    }
    epoch_out = static_cast<time_t>(store().getULong("last_chk", 0));
    store().end();
}

inline void saveLastCheckedEpoch(time_t epoch) {
    if (!store().begin("mbta_v3", false)) return;
    store().putULong("last_chk", static_cast<uint32_t>(epoch));
    store().end();
}

inline void loadQuoteIndex(uint16_t &index_out) {
    if (!store().begin("mbta_v3", true)) {
        index_out = 0;
        return;
    }
    index_out = store().getUShort("quote_ix", 0);
    store().end();
}

inline void saveQuoteIndex(uint16_t index) {
    if (!store().begin("mbta_v3", false)) return;
    store().putUShort("quote_ix", index);
    store().end();
}

inline uint16_t advanceQuoteIndex(uint16_t current, size_t count) {
    const uint16_t next = static_cast<uint16_t>((current + 1) % count);
    saveQuoteIndex(next);
    return next;
}

inline void formatLastCheckedLine(time_t epoch, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    if (epoch <= 0) {
        snprintf(out, out_len, "Not checked yet");
        return;
    }
    struct tm local_tm = {};
    localtime_r(&epoch, &local_tm);
    char daybuf[12];
    char timebuf[16];
    strftime(daybuf, sizeof(daybuf), "%a", &local_tm);
    strftime(timebuf, sizeof(timebuf), "%I:%M %p", &local_tm);
    if (timebuf[0] == '0') {
        snprintf(out, out_len, "Last checked %s %s", daybuf, timebuf + 1);
    } else {
        snprintf(out, out_len, "Last checked %s %s", daybuf, timebuf);
    }
}

}  // namespace standby_prefs
