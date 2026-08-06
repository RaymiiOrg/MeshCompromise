#pragma once

#include <cstdint>

#include "meshcompromise/lora_profile.h"
#include "meshcompromise/radio_ops.h"

namespace meshcompromise
{

enum class SliceState { Meshtastic, MeshcoreScan, MeshcoreDwell };

struct SliceConfig {
    bool enabled = true;
    bool adaptive = true;
    uint32_t meshtasticHoldMs = 0;
    uint32_t scanDwellMs = 0;
    uint32_t maxDwellMs = 4000;
    uint32_t busyRetryMs = 5;
    uint32_t pumpIntervalMs = 2;
};

struct SliceStats {
    uint32_t meshcoreListenMs = 0;
    uint32_t meshtasticListenMs = 0;
    uint32_t slicesTaken = 0;
    uint32_t slicesSkippedBusy = 0;
    uint32_t cadPositive = 0;
    uint32_t cadNegative = 0;
    uint32_t dwellTimeouts = 0;

    uint32_t totalMs() const { return meshcoreListenMs + meshtasticListenMs; }
    float meshcoreDutyCycle() const;
};

class SliceScheduler
{
  public:
    SliceScheduler(RadioOps &ops, const SliceConfig &config);

    void setProfiles(const LoraProfile &meshtastic, const LoraProfile &meshcore);
    void setConfig(const SliceConfig &config);
    void setEnabled(bool enabled);

    const SliceConfig &config() const { return config_; }
    SwitchMode mode() const { return mode_; }
    SliceState state() const { return state_; }
    const SliceStats &stats() const { return stats_; }
    void resetStats();

    uint32_t tick(uint32_t nowMs);

  private:
    void applyDerivedTiming();
    void accumulate(uint32_t nowMs);
    uint32_t handleMeshtastic(uint32_t nowMs);
    uint32_t handleScan(uint32_t nowMs);
    uint32_t handleDwell(uint32_t nowMs);
    void beginSlice(uint32_t nowMs);
    void endSlice(uint32_t nowMs);

    RadioOps &ops_;
    SliceConfig config_;
    LoraProfile meshtasticProfile_;
    LoraProfile meshcoreProfile_;
    SwitchMode mode_ = SwitchMode::Split;
    SliceState state_ = SliceState::Meshtastic;
    SliceStats stats_;

    bool started_ = false;
    uint32_t lastTickMs_ = 0;
    uint32_t stateEnteredMs_ = 0;
    uint32_t dwellStartedMs_ = 0;
    uint32_t consecutiveQuietSlices_ = 0;
};

} // namespace meshcompromise
