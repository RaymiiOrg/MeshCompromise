#include "meshcompromise/bridge_cycle.h"

#include "meshcompromise/bridge_log.h"

namespace meshcompromise
{

void BridgeCycle::setIntervals(uint32_t now, uint16_t advertMinutes, uint16_t statsMinutes)
{
    if (advertMinutes != advertTimer_.intervalMinutes()) {
        advertTimer_.setIntervalMinutes(advertMinutes);
        if (started_)
            advertTimer_.rearm(now, kFirstAdvertDelayMs);
        MC_LOG_DEBUG("advert interval is now %umin", static_cast<unsigned>(advertMinutes));
    }

    if (statsMinutes != statsTimer_.intervalMinutes()) {
        statsTimer_.setIntervalMinutes(statsMinutes);
        if (started_)
            statsTimer_.rearm(now, kFirstStatsDelayMs);
        MC_LOG_DEBUG("stats interval is now %umin", static_cast<unsigned>(statsMinutes));
    }
}

CycleActions BridgeCycle::tick(uint32_t now)
{
    CycleActions actions;

    if (!started_) {
        started_ = true;
        startedMs_ = now;
        lastProfileCheckMs_ = now;
        advertTimer_.start(now, kFirstAdvertDelayMs);
        statsTimer_.start(now, kFirstStatsDelayMs);
        actions.firstTick = true;
        actions.refreshProfiles = true;
    } else if (now - lastProfileCheckMs_ >= kProfileRefreshIntervalMs) {
        lastProfileCheckMs_ = now;
        actions.refreshProfiles = true;
    }

    actions.advertDue = advertTimer_.due(now);
    actions.statsDue = statsTimer_.due(now);
    actions.uptimeSeconds = uptimeSeconds(now);
    return actions;
}

} // namespace meshcompromise
