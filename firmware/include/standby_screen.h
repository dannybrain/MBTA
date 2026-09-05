#pragma once

#include <stddef.h>
#include <time.h>

namespace lilygo_t5 {
class LilyGoT5Display;
}

namespace standby_screen {

void render(lilygo_t5::LilyGoT5Display &display, int battery_percent,
            time_t last_checked_epoch, size_t quote_index);

// Static screen shown when waiting for RST button press
void renderStatic(lilygo_t5::LilyGoT5Display &display, int battery_percent,
                  time_t last_checked_epoch, size_t quote_index);

// Shown immediately on wake tap while WiFi connects and data loads.
void renderLoading(lilygo_t5::LilyGoT5Display &display, int battery_percent);

}  // namespace standby_screen
