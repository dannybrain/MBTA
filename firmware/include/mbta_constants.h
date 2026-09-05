#pragma once

#include <stdint.h>

static constexpr const char *kSummitStop = "place-sumav";
static constexpr const char *kClevelandStop = "place-clmnl";
static constexpr uint8_t kInboundDirectionId = 1;

static constexpr int kDefaultTravelMinutes = 8;
static constexpr uint32_t kScheduleCacheTtlMs = 30UL * 60UL * 1000UL;
static constexpr uint32_t kServiceWindowRefreshMs = 6UL * 3600UL * 1000UL;

// Active session: live trains after tap (WiFi on).
static constexpr uint32_t kActiveTimeoutMs = 90UL * 1000UL;
static constexpr uint32_t kStandbySleepMs = 1000UL;
