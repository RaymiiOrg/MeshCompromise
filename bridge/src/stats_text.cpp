#include "meshcompromise/stats_text.h"

#include <cmath>

#include <cstdio>

namespace meshcompromise
{

namespace
{

void writeUptime(char *out, size_t capacity, uint32_t seconds)
{
    if (seconds < 3600) {
        snprintf(out, capacity, "%um", seconds / 60);
        return;
    }
    if (seconds < 86400) {
        snprintf(out, capacity, "%uh%um", seconds / 3600, (seconds % 3600) / 60);
        return;
    }
    snprintf(out, capacity, "%ud%uh", seconds / 86400, (seconds % 86400) / 3600);
}

} // namespace

size_t buildStatsText(const StatsSnapshot &stats, char *out, size_t capacity)
{
    if (out == nullptr || capacity == 0)
        return 0;

    char uptime[16] = {0};
    writeUptime(uptime, sizeof(uptime), stats.uptimeSeconds);

    const int written =
        snprintf(out, capacity, "up %s rx%lu tx%lu mir%lu/%lu adv%lu duty%u%% hold%lu sw%lu heap%luk", uptime,
                 static_cast<unsigned long>(stats.meshcoreHeard), static_cast<unsigned long>(stats.meshcoreSent),
                 static_cast<unsigned long>(stats.mirroredOut), static_cast<unsigned long>(stats.mirroredIn),
                 static_cast<unsigned long>(stats.adverts),
                 static_cast<unsigned>(std::lround(stats.meshcoreDutyCycle * 100.0f)),
                 static_cast<unsigned long>(stats.holdMs), static_cast<unsigned long>(stats.switchOverheadMs),
                 static_cast<unsigned long>(stats.freeHeapBytes / 1024));

    if (written <= 0)
        return 0;
    return static_cast<size_t>(written) < capacity ? static_cast<size_t>(written) : capacity - 1;
}

} // namespace meshcompromise
