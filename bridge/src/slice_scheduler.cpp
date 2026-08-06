#include "meshcompromise/slice_scheduler.h"

#include <algorithm>

#include "meshcompromise/airtime.h"

namespace meshcompromise
{

namespace
{
constexpr uint32_t kQuietSlicesBeforeBackoff = 8;
constexpr uint32_t kMaxHoldMultiplier = 4;
} // namespace

float SliceStats::meshcoreDutyCycle() const
{
    const uint32_t total = totalMs();
    if (total == 0)
        return 0.0f;
    return static_cast<float>(meshcoreListenMs) / static_cast<float>(total);
}

SliceScheduler::SliceScheduler(RadioOps &ops, const SliceConfig &config) : ops_(ops), config_(config)
{
    meshcoreProfile_ = meshcoreDefaultProfile();
    meshtasticProfile_ = meshtasticNarrowSlowProfile();
    applyDerivedTiming();
}

void SliceScheduler::setProfiles(const LoraProfile &meshtastic, const LoraProfile &meshcore)
{
    meshtasticProfile_ = meshtastic;
    meshcoreProfile_ = meshcore;
    applyDerivedTiming();
}

void SliceScheduler::setConfig(const SliceConfig &config)
{
    config_ = config;
    applyDerivedTiming();
}

void SliceScheduler::setEnabled(bool enabled)
{
    config_.enabled = enabled;
}

void SliceScheduler::resetStats()
{
    stats_ = SliceStats();
}

void SliceScheduler::applyDerivedTiming()
{
    mode_ = selectSwitchMode(meshtasticProfile_, meshcoreProfile_);

    if (config_.meshtasticHoldMs == 0)
        config_.meshtasticHoldMs = recommendedScanPeriodMs(meshcoreProfile_);

    const uint32_t detect = minimumDetectDwellMs(meshcoreProfile_);
    if (config_.scanDwellMs < detect)
        config_.scanDwellMs = detect;

    const uint32_t worstCase = static_cast<uint32_t>(packetAirtimeMs(meshcoreProfile_, 184)) + detect;
    if (config_.maxDwellMs < worstCase)
        config_.maxDwellMs = worstCase;
}

void SliceScheduler::accumulate(uint32_t nowMs)
{
    if (!started_) {
        started_ = true;
        lastTickMs_ = nowMs;
        stateEnteredMs_ = nowMs;
        return;
    }

    const uint32_t elapsed = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;

    if (state_ == SliceState::Meshtastic)
        stats_.meshtasticListenMs += elapsed;
    else
        stats_.meshcoreListenMs += elapsed;
}

uint32_t SliceScheduler::tick(uint32_t nowMs)
{
    accumulate(nowMs);

    switch (state_) {
    case SliceState::Meshtastic:
        return handleMeshtastic(nowMs);
    case SliceState::MeshcoreScan:
        return handleScan(nowMs);
    case SliceState::MeshcoreDwell:
        return handleDwell(nowMs);
    }
    return config_.meshtasticHoldMs;
}

uint32_t SliceScheduler::handleMeshtastic(uint32_t nowMs)
{
    if (!config_.enabled)
        return config_.meshtasticHoldMs;

    uint32_t hold = config_.meshtasticHoldMs;
    if (config_.adaptive && consecutiveQuietSlices_ >= kQuietSlicesBeforeBackoff) {
        const uint32_t multiplier =
            std::min<uint32_t>(kMaxHoldMultiplier, 1 + consecutiveQuietSlices_ / kQuietSlicesBeforeBackoff);
        hold = config_.meshtasticHoldMs * multiplier;
    }

    const uint32_t held = nowMs - stateEnteredMs_;
    if (held < hold)
        return hold - held;

    if (ops_.meshtasticBusy()) {
        stats_.slicesSkippedBusy++;
        return config_.busyRetryMs;
    }

    beginSlice(nowMs);
    return config_.scanDwellMs;
}

uint32_t SliceScheduler::handleScan(uint32_t nowMs)
{
    const bool wanted = ops_.meshcoreTxPending() || ops_.meshcoreTxBusy();
    const bool active = ops_.meshcoreReceiving() || ops_.channelActive();

    if (active || wanted) {
        if (active) {
            stats_.cadPositive++;
            consecutiveQuietSlices_ = 0;
        }
        state_ = SliceState::MeshcoreDwell;
        stateEnteredMs_ = nowMs;
        dwellStartedMs_ = nowMs;
        ops_.pumpMeshcore();
        return config_.pumpIntervalMs;
    }

    stats_.cadNegative++;
    consecutiveQuietSlices_++;
    endSlice(nowMs);
    return config_.meshtasticHoldMs;
}

uint32_t SliceScheduler::handleDwell(uint32_t nowMs)
{
    ops_.pumpMeshcore();

    const bool busy = ops_.meshcoreReceiving() || ops_.meshcoreTxBusy() || ops_.meshcoreTxPending();
    if (busy) {
        if (nowMs - dwellStartedMs_ >= config_.maxDwellMs) {
            stats_.dwellTimeouts++;
            endSlice(nowMs);
            return config_.meshtasticHoldMs;
        }
        return config_.pumpIntervalMs;
    }

    endSlice(nowMs);
    return config_.meshtasticHoldMs;
}

void SliceScheduler::beginSlice(uint32_t nowMs)
{
    ops_.enterMeshcore(mode_, meshcoreProfile_);
    state_ = SliceState::MeshcoreScan;
    stateEnteredMs_ = nowMs;
    stats_.slicesTaken++;
}

void SliceScheduler::endSlice(uint32_t nowMs)
{
    ops_.leaveMeshcore(mode_, meshtasticProfile_);
    state_ = SliceState::Meshtastic;
    stateEnteredMs_ = nowMs;
}

} // namespace meshcompromise
