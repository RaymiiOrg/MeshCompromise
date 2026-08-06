#pragma once

#include <cstddef>
#include <cstdint>

namespace meshcompromise

{

constexpr size_t kStatsTextLength = 112;

struct StatsSnapshot {
    uint32_t uptimeSeconds = 0;
    uint32_t meshcoreHeard = 0;
    uint32_t meshcoreSent = 0;
    uint32_t mirroredOut = 0;
    uint32_t mirroredIn = 0;
    uint32_t adverts = 0;
    float meshcoreDutyCycle = 0.0f;
    uint32_t freeHeapBytes = 0;
    uint32_t holdMs = 0;
    uint32_t switchOverheadMs = 0;
};

size_t buildStatsText(const StatsSnapshot &stats, char *out, size_t capacity);

} // namespace meshcompromise
