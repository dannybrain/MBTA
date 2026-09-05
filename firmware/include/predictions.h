#pragma once

#include <Arduino.h>
#include <time.h>

enum class PredSource : uint8_t { None = 0, Live, Est, Sched };

struct TrainEstimate {
    int minutes = -1;
    time_t arrival_epoch = 0;
    String clock_time;
    PredSource source = PredSource::None;
};

const char *predSourceLabel(PredSource source);

void predictionsResetCache();

bool predictionsEnsureTime();

void refreshTrainEstimate(TrainEstimate &train);

void refreshTrainEstimates(TrainEstimate &next, TrainEstimate &then);

bool fetchSummitArrivals(TrainEstimate &next, TrainEstimate &then, String &error);
