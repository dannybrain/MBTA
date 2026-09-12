#include "predictions.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

#include "mbta_constants.h"
#include "secrets.h"

static constexpr const char *kRoute = "Green-C";
static constexpr uint8_t kDirectionId = 1;
static constexpr size_t kMaxCandidates = 16;
static constexpr size_t kSchedulePageLimit = 500;

static int g_travel_minutes = kDefaultTravelMinutes;
static uint32_t g_schedule_cache_ms = 0;

struct Candidate {
    int minutes;
    time_t arrival_epoch;
    char clock_time[16];
    PredSource source;
    char trip_id[24];
};

static Candidate g_schedule_cache[kMaxCandidates];
static size_t g_schedule_cache_count = 0;
static Candidate g_candidates[kMaxCandidates];
static Candidate g_merged[kMaxCandidates];
static Candidate g_sorted[kMaxCandidates];

// Use smaller, scoped JsonDocument instances instead of global
// This reduces memory pressure and allows better memory management

static int g_last_http_code = 0;

static int sourceRank(PredSource source) {
    switch (source) {
        case PredSource::Live:
            return 0;
        case PredSource::Est:
            return 1;
        case PredSource::Sched:
            return 2;
        default:
            return 3;
    }
}

const char *predSourceLabel(PredSource source) {
    switch (source) {
        case PredSource::Live:
            return "LIVE";
        case PredSource::Est:
            return "EST";
        case PredSource::Sched:
            return "SCHED";
        default:
            return "";
    }
}

void predictionsResetCache() {
    g_travel_minutes = kDefaultTravelMinutes;
    g_schedule_cache_count = 0;
    g_schedule_cache_ms = 0;
}

bool predictionsEnsureTime() {
    if (time(nullptr) > 1700000000) return true;

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();

    for (int i = 0; i < 40; ++i) {
        if (time(nullptr) > 1700000000) return true;
        delay(250);
    }
    return false;
}

static bool parseIso8601Epoch(const char *iso, time_t &epoch_out) {
    if (!iso || !iso[0]) return false;

    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 0;
    int mi = 0;
    int s = 0;
    int offset_sec = 0;

    if (strstr(iso, "Z") != nullptr) {
        if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
            return false;
        }
    } else {
        char tzsign = '+';
        int tzh = 0;
        int tzm = 0;
        const int matched = sscanf(iso, "%d-%d-%dT%d:%d:%d%c%d:%d", &y, &mo, &d, &h, &mi, &s,
                                   &tzsign, &tzh, &tzm);
        if (matched < 6) return false;
        if (matched >= 9) {
            offset_sec = (tzh * 3600 + tzm * 60) * (tzsign == '-' ? -1 : 1);
        }
    }

    struct tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    setenv("TZ", "UTC0", 1);
    tzset();
    const time_t wall_as_utc = mktime(&tm);
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
    if (wall_as_utc == (time_t)-1) return false;
    epoch_out = wall_as_utc - offset_sec;
    return true;
}

static void formatLocalClock(time_t epoch, char *out, size_t out_len) {
    struct tm local_tm = {};
    localtime_r(&epoch, &local_tm);
    char buf[16];
    strftime(buf, sizeof(buf), "%I:%M %p", &local_tm);
    if (buf[0] == '0') {
        strncpy(out, buf + 1, out_len - 1);
    } else {
        strncpy(out, buf, out_len - 1);
    }
    out[out_len - 1] = '\0';
}

static int minutesUntilEpoch(time_t arrival_epoch) {
    const time_t now = time(nullptr);
    if (arrival_epoch == (time_t)-1 || now == (time_t)-1) return -1;
    return (int)lround(difftime(arrival_epoch, now) / 60.0);
}

static bool mbtaGet(const String &url, String &payload_out) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);  // Reduced from 15s to 10s for faster timeout
    g_last_http_code = 0;
    if (!http.begin(client, url)) return false;
    g_last_http_code = http.GET();
    if (g_last_http_code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    payload_out = http.getString();
    http.end();
    payload_out.trim();
    return payload_out.length() > 0;
}

static void releaseHttpMemory(String &payload) {
    payload = String();
}

static void sortCandidates(Candidate *items, size_t count) {
    // Improved quicksort-like algorithm for better performance
    if (count <= 1) return;
    
    // Simple insertion sort for small arrays (more efficient than quicksort for n < 10)
    if (count <= 10) {
        for (size_t i = 1; i < count; ++i) {
            Candidate key = items[i];
            size_t j = i;
            while (j > 0) {
                const bool swap = key.minutes < items[j - 1].minutes ||
                                 (key.minutes == items[j - 1].minutes &&
                                  sourceRank(key.source) < sourceRank(items[j - 1].source));
                if (!swap) break;
                items[j] = items[j - 1];
                j--;
            }
            items[j] = key;
        }
        return;
    }
    
    // Quick sort for larger arrays
    size_t pivot = count / 2;
    Candidate pivot_value = items[pivot];
    
    // Move pivot to end
    items[pivot] = items[count - 1];
    items[count - 1] = pivot_value;
    
    size_t store = 0;
    for (size_t i = 0; i < count - 1; ++i) {
        const bool less = items[i].minutes < pivot_value.minutes ||
                         (items[i].minutes == pivot_value.minutes &&
                          sourceRank(items[i].source) < sourceRank(pivot_value.source));
        if (less) {
            Candidate tmp = items[store];
            items[store] = items[i];
            items[i] = tmp;
            store++;
        }
    }
    
    // Move pivot to its final place
    Candidate tmp = items[store];
    items[store] = items[count - 1];
    items[count - 1] = tmp;
    
    // Recursively sort
    sortCandidates(items, store);
    sortCandidates(items + store + 1, count - store - 1);
}

static bool tripsEqual(const char *a, const char *b) {
    if (!a || !b || !a[0] || !b[0]) return false;
    return strcmp(a, b) == 0;
}

static void writeCandidate(Candidate &c, int minutes, time_t epoch, PredSource source,
                           const char *trip_id) {
    c.minutes = minutes;
    c.arrival_epoch = epoch;
    formatLocalClock(epoch, c.clock_time, sizeof(c.clock_time));
    c.source = source;
    if (trip_id) {
        strncpy(c.trip_id, trip_id, sizeof(c.trip_id) - 1);
        c.trip_id[sizeof(c.trip_id) - 1] = '\0';
    } else {
        c.trip_id[0] = '\0';
    }
}

static void storeCandidate(Candidate *list, size_t &count, int minutes, time_t epoch,
                           PredSource source, const char *trip_id) {
    if (count >= kMaxCandidates) return;
    writeCandidate(list[count], minutes, epoch, source, trip_id);
    count++;
}

static void appendUniqueScheduleCandidate(int minutes, time_t epoch, PredSource source,
                                          const char *trip_id) {
    for (size_t i = 0; i < g_schedule_cache_count; ++i) {
        if (tripsEqual(g_schedule_cache[i].trip_id, trip_id)) {
            if (sourceRank(source) < sourceRank(g_schedule_cache[i].source)) {
                writeCandidate(g_schedule_cache[i], minutes, epoch, source, trip_id);
            }
            return;
        }
    }
    storeCandidate(g_schedule_cache, g_schedule_cache_count, minutes, epoch, source, trip_id);
}

static void parseSchedulePayload(const String &payload, PredSource source, int travel_offset_min) {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) return;

    for (JsonObject item : doc["data"].as<JsonArray>()) {
        const char *stamp = item["attributes"]["arrival_time"];
        if (!stamp) stamp = item["attributes"]["departure_time"];
        if (!stamp) continue;

        time_t epoch = 0;
        if (!parseIso8601Epoch(stamp, epoch)) continue;
        if (travel_offset_min > 0) {
            epoch += static_cast<time_t>(travel_offset_min) * 60;
        }

        const int mins = minutesUntilEpoch(epoch);
        if (mins < 0) continue;

        const char *trip_id = item["relationships"]["trip"]["data"]["id"];
        appendUniqueScheduleCandidate(mins, epoch, source, trip_id);
    }
}

static void refreshScheduleCache() {
    g_schedule_cache_count = 0;

    String url = String("https://api-v3.mbta.com/schedules?filter[stop]=") + kSummitStop +
                 "&filter[route]=" + kRoute + "&filter[direction_id]=" + String(kDirectionId) +
                 "&sort=arrival_time&page[limit]=" + String(kSchedulePageLimit) +
                 "&api_key=" + MBTA_API_KEY;
    String payload;
    if (mbtaGet(url, payload)) {
        parseSchedulePayload(payload, PredSource::Sched, 0);
    }
    releaseHttpMemory(payload);

    url = String("https://api-v3.mbta.com/schedules?filter[stop]=") + kClevelandStop +
          "&filter[route]=" + kRoute + "&filter[direction_id]=" + String(kDirectionId) +
          "&sort=departure_time&page[limit]=" + String(kSchedulePageLimit) +
          "&api_key=" + MBTA_API_KEY;
    if (mbtaGet(url, payload)) {
        parseSchedulePayload(payload, PredSource::Est, g_travel_minutes);
    }
    releaseHttpMemory(payload);
    g_schedule_cache_ms = millis();
}

static void ensureScheduleCacheFresh() {
    if (g_schedule_cache_count > 0 &&
        (millis() - g_schedule_cache_ms) < kScheduleCacheTtlMs) {
        return;
    }
    refreshScheduleCache();
}

static void appendScheduleCandidates(size_t &count) {
    ensureScheduleCacheFresh();
    for (size_t i = 0; i < g_schedule_cache_count && count < kMaxCandidates; ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < count; ++j) {
            if (tripsEqual(g_candidates[j].trip_id, g_schedule_cache[i].trip_id)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            g_candidates[count++] = g_schedule_cache[i];
        }
    }
}

static void loadPredictionCandidates(const char *stop_id, PredSource vehicle_source,
                                     PredSource fallback_source, int travel_offset_min,
                                     bool prefer_departure, size_t &count) {
    if (count >= kMaxCandidates) return;

    String url = String("https://api-v3.mbta.com/predictions?filter[stop]=") + stop_id +
                 "&filter[route]=" + kRoute + "&filter[direction_id]=" + String(kDirectionId) +
                 "&sort=arrival_time&page[limit]=20&include=vehicle&api_key=" + MBTA_API_KEY;

    String payload;
    if (!mbtaGet(url, payload)) return;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        releaseHttpMemory(payload);
        return;
    }

    for (JsonObject item : doc["data"].as<JsonArray>()) {
        if (count >= kMaxCandidates) break;
        const char *stamp = prefer_departure ? item["attributes"]["departure_time"]
                                             : item["attributes"]["arrival_time"];
        if (!stamp) {
            stamp = prefer_departure ? item["attributes"]["arrival_time"]
                                     : item["attributes"]["departure_time"];
        }
        if (!stamp) continue;

        time_t epoch = 0;
        if (!parseIso8601Epoch(stamp, epoch)) continue;
        if (travel_offset_min > 0) {
            epoch += static_cast<time_t>(travel_offset_min) * 60;
        }

        const int mins = minutesUntilEpoch(epoch);
        if (mins < 0) continue;

        PredSource source = fallback_source;
        if (vehicle_source == PredSource::Live) {
            JsonObject veh_ref = item["relationships"]["vehicle"];
            if (!veh_ref["data"].isNull()) {
                source = PredSource::Live;
            }
        } else if (vehicle_source == PredSource::Est) {
            source = PredSource::Est;
        }

        const char *trip_id = item["relationships"]["trip"]["data"]["id"];
        storeCandidate(g_candidates, count, mins, epoch, source, trip_id);
    }
    releaseHttpMemory(payload);
}

static String formatLocalClockToString(time_t epoch) {
    char buf[16];
    formatLocalClock(epoch, buf, sizeof(buf));
    return String(buf);
}

static constexpr size_t kDisplayTrainCount = 2;

static bool fetchSimpleSummitFallback(TrainEstimate &next, TrainEstimate &then) {
    String url = String("https://api-v3.mbta.com/predictions?filter[stop]=") + kSummitStop +
                 "&filter[route]=" + kRoute + "&filter[direction_id]=" + String(kDirectionId) +
                 "&sort=arrival_time&page[limit]=8&include=vehicle&api_key=" + MBTA_API_KEY;

    String payload;
    if (!mbtaGet(url, payload)) return false;

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        releaseHttpMemory(payload);
        return false;
    }

    size_t found = 0;
    TrainEstimate results[kDisplayTrainCount];

    for (JsonObject item : doc["data"].as<JsonArray>()) {
        if (found >= kDisplayTrainCount) break;
        const char *stamp = item["attributes"]["arrival_time"];
        if (!stamp) stamp = item["attributes"]["departure_time"];
        if (!stamp) continue;

        time_t epoch = 0;
        if (!parseIso8601Epoch(stamp, epoch)) continue;
        const int mins = minutesUntilEpoch(epoch);
        if (mins < 0) continue;

        JsonObject veh_ref = item["relationships"]["vehicle"];
        results[found].minutes = mins;
        results[found].arrival_epoch = epoch;
        results[found].clock_time = String(formatLocalClockToString(epoch));
        results[found].source =
            !veh_ref["data"].isNull() ? PredSource::Live : PredSource::Sched;
        found++;
    }
    releaseHttpMemory(payload);

    if (found == 0) return false;
    next = results[0];
    then = (found > 1) ? results[1] : TrainEstimate{};
    return true;
}

static void candidateToEstimate(const Candidate &candidate, TrainEstimate &out) {
    out.minutes = candidate.minutes;
    out.arrival_epoch = candidate.arrival_epoch;
    out.clock_time = candidate.clock_time;
    out.source = candidate.source;
}

static void recomputeCandidateMinutes(Candidate &c) {
    if (c.arrival_epoch <= 0) {
        c.minutes = -1;
        return;
    }
    const int mins = minutesUntilEpoch(c.arrival_epoch);
    c.minutes = mins;
    if (mins >= 0) {
        formatLocalClock(c.arrival_epoch, c.clock_time, sizeof(c.clock_time));
    }
}

static size_t recomputeAndFilterCandidates(size_t count) {
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
        recomputeCandidateMinutes(g_candidates[i]);
        if (g_candidates[i].minutes < 0) continue;
        if (kept != i) {
            g_candidates[kept] = g_candidates[i];
        }
        kept++;
    }
    return kept;
}

static void promoteThenTrain(TrainEstimate &next, TrainEstimate &then) {
    if (next.minutes >= 0) return;
    if (then.minutes < 0) return;
    if (then.source != PredSource::Live) return;
    next = then;
    then = TrainEstimate{};
    refreshTrainEstimate(next);
}

void refreshTrainEstimate(TrainEstimate &train) {
    if (train.arrival_epoch <= 0) return;
    const int mins = minutesUntilEpoch(train.arrival_epoch);
    if (mins < 0) {
        train.minutes = -1;
        train.clock_time = "";
        return;
    }
    train.minutes = mins;
    char buf[16];
    formatLocalClock(train.arrival_epoch, buf, sizeof(buf));
    train.clock_time = buf;
}

void refreshTrainEstimates(TrainEstimate &next, TrainEstimate &then) {
    refreshTrainEstimate(next);
    refreshTrainEstimate(then);
    promoteThenTrain(next, then);
}

static bool tripAlreadyPicked(const char *trip_id, const char picked_ids[][24], size_t picked_count) {
    if (!trip_id || !trip_id[0]) return false;
    for (size_t i = 0; i < picked_count; ++i) {
        if (tripsEqual(trip_id, picked_ids[i])) return true;
    }
    return false;
}

static void tryPickCandidateToArray(const Candidate &c, char picked_ids[][24], size_t &picked_count,
                                    Candidate *picked) {
    if (tripAlreadyPicked(c.trip_id, picked_ids, picked_count)) return;
    strncpy(picked_ids[picked_count], c.trip_id, sizeof(picked_ids[picked_count]) - 1);
    picked_ids[picked_count][sizeof(picked_ids[picked_count]) - 1] = '\0';
    picked[picked_count++] = c;
}

static bool isEstimate(PredSource source) {
    return source == PredSource::Est || source == PredSource::Sched;
}

static bool tripInPickedList(const char *trip_id, const Candidate *picked, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (tripsEqual(trip_id, picked[i].trip_id)) return true;
    }
    return false;
}

static void assignPickedToEstimates(const Candidate *picked, size_t count, TrainEstimate &next,
                                    TrainEstimate &then) {
    next = TrainEstimate{};
    then = TrainEstimate{};
    if (count == 0) return;

    candidateToEstimate(picked[0], next);
    if (count > 1) {
        candidateToEstimate(picked[1], then);
    }
}

static bool scrubEstimatesBeforeLive(Candidate *picked, size_t &count) {
    if (count < 2) return false;

    for (size_t i = 0; i + 1 < count; ++i) {
        if (isEstimate(picked[i].source) && picked[i + 1].source == PredSource::Live &&
            picked[i].minutes <= picked[i + 1].minutes) {
            for (size_t j = i + 1; j < count; ++j) {
                picked[j - 1] = picked[j];
            }
            count--;
            return true;
        }
    }
    return false;
}

static void scrubAllEstimatesBeforeLive(Candidate *picked, size_t &count) {
    while (scrubEstimatesBeforeLive(picked, count)) {
    }
}

static bool tryListForNextTrain(const Candidate *items, size_t item_count, const Candidate *picked,
                                size_t picked_count, int after_minutes, PredSource required,
                                Candidate &out) {
    for (size_t i = 0; i < item_count; ++i) {
        const Candidate &c = items[i];
        if (c.minutes <= after_minutes) continue;
        if (tripInPickedList(c.trip_id, picked, picked_count)) continue;
        if (required != PredSource::None && c.source != required) continue;
        out = c;
        return true;
    }
    return false;
}

static bool findNextTrainAfter(const Candidate *picked, size_t picked_count, size_t merged_count,
                               size_t pool_count, int after_minutes, Candidate &out) {
    if (tryListForNextTrain(g_merged, merged_count, picked, picked_count, after_minutes,
                            PredSource::Live, out)) {
        return true;
    }
    if (tryListForNextTrain(g_merged, merged_count, picked, picked_count, after_minutes,
                            PredSource::Est, out)) {
        return true;
    }
    if (tryListForNextTrain(g_merged, merged_count, picked, picked_count, after_minutes,
                            PredSource::Sched, out)) {
        return true;
    }
    if (tryListForNextTrain(g_sorted, pool_count, picked, picked_count, after_minutes,
                            PredSource::Live, out)) {
        return true;
    }
    if (tryListForNextTrain(g_sorted, pool_count, picked, picked_count, after_minutes,
                            PredSource::Est, out)) {
        return true;
    }
    return tryListForNextTrain(g_sorted, pool_count, picked, picked_count, after_minutes,
                               PredSource::Sched, out);
}

static void refillPickedTrains(Candidate *picked, size_t &count, size_t merged_count,
                               size_t pool_count) {
    constexpr size_t kTarget = kDisplayTrainCount;

    while (count < kTarget) {
        const int after = count > 0 ? picked[count - 1].minutes : -1;
        Candidate next_c{};
        if (!findNextTrainAfter(picked, count, merged_count, pool_count, after, next_c)) {
            break;
        }
        picked[count++] = next_c;
    }
}

static void refinePickedTrains(Candidate *picked, size_t &count, size_t merged_count,
                               size_t pool_count) {
    scrubAllEstimatesBeforeLive(picked, count);
    refillPickedTrains(picked, count, merged_count, pool_count);
}

static void pickDistinctTrains(size_t count, TrainEstimate &next, TrainEstimate &then) {
    count = recomputeAndFilterCandidates(count);
    size_t merged_count = 0;

    for (size_t i = 0; i < count; ++i) {
        const Candidate &c = g_candidates[i];
        bool replaced = false;
        for (size_t j = 0; j < merged_count; ++j) {
            if (tripsEqual(g_merged[j].trip_id, c.trip_id)) {
                if (sourceRank(c.source) < sourceRank(g_merged[j].source)) {
                    g_merged[j] = c;
                }
                replaced = true;
                break;
            }
        }
        if (!replaced && merged_count < kMaxCandidates) {
            g_merged[merged_count++] = c;
        }
    }

    sortCandidates(g_merged, merged_count);

    next = TrainEstimate{};
    then = TrainEstimate{};
    if (merged_count == 0) return;

    size_t sorted_count = count < kMaxCandidates ? count : kMaxCandidates;
    for (size_t i = 0; i < sorted_count; ++i) {
        g_sorted[i] = g_candidates[i];
    }
    sortCandidates(g_sorted, sorted_count);

    Candidate picked[kDisplayTrainCount];
    size_t picked_count = 0;
    char picked_ids[kDisplayTrainCount][24] = {};

    for (size_t i = 0; i < merged_count; ++i) {
        if (g_merged[i].source == PredSource::Live) {
            tryPickCandidateToArray(g_merged[i], picked_ids, picked_count, picked);
            break;
        }
    }
    if (picked_count == 0 && merged_count > 0) {
        tryPickCandidateToArray(g_merged[0], picked_ids, picked_count, picked);
    }

    refinePickedTrains(picked, picked_count, merged_count, sorted_count);
    assignPickedToEstimates(picked, picked_count, next, then);
}

bool fetchSummitArrivals(TrainEstimate &next, TrainEstimate &then, String &error) {
    if (!predictionsEnsureTime()) {
        error = "Time sync";
        return false;
    }

    size_t count = 0;

    loadPredictionCandidates(kSummitStop, PredSource::Live, PredSource::Sched, 0, false, count);
    loadPredictionCandidates(kClevelandStop, PredSource::Live, PredSource::Est, g_travel_minutes,
                             true, count);
    appendScheduleCandidates(count);

    if (count == 0) {
        if (!fetchSimpleSummitFallback(next, then)) {
            if (g_last_http_code > 0) {
                error = String("API HTTP ") + g_last_http_code;
            } else {
                error = "No trains";
            }
            return false;
        }
    } else {
        pickDistinctTrains(count, next, then);
    }

    if (next.arrival_epoch <= 0) {
        error = "No trains";
        return false;
    }

    error = "";
    return true;
}
