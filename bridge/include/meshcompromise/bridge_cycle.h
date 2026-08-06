#pragma once

#include <cstdint>

#include "meshcompromise/periodic.h"

namespace meshcompromise
{

// RadioArbiter::leaveMeshcore() now restores Meshtastic's sync word/preamble
// directly from this cached profile in Aligned mode instead of always asking
// the host radio to reconfigure itself from live config.lora - so how stale
// this cache is allowed to get directly bounds how long a live Meshtastic
// LoRa settings change take to actually reach the radio. Kept short for that
// reason; it used to only need to be "occasional".
constexpr uint32_t kProfileRefreshIntervalMs = 250;
constexpr uint32_t kFirstAdvertDelayMs = 20000;
constexpr uint32_t kFirstStatsDelayMs = 30000;

struct CycleActions {
    bool firstTick = false;
    bool refreshProfiles = false;
    bool advertDue = false;
    bool statsDue = false;
    uint32_t uptimeSeconds = 0;
};

class BridgeCycle
{
  public:
    void setIntervals(uint32_t now, uint16_t advertMinutes, uint16_t statsMinutes);
    CycleActions tick(uint32_t now);

    bool started() const { return started_; }
    uint32_t uptimeSeconds(uint32_t now) const { return started_ ? (now - startedMs_) / 1000 : 0; }
    const PeriodicTimer &advertTimer() const { return advertTimer_; }
    const PeriodicTimer &statsTimer() const { return statsTimer_; }

    static uint32_t clampDelay(uint32_t delay) { return delay == 0 ? 1 : delay; }

  private:
    PeriodicTimer advertTimer_;
    PeriodicTimer statsTimer_;
    uint32_t startedMs_ = 0;
    uint32_t lastProfileCheckMs_ = 0;
    bool started_ = false;
};

} // namespace meshcompromise
