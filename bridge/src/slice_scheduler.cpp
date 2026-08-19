#include "meshcompromise/slice_scheduler.h"

#include <algorithm>

#include "meshcompromise/airtime.h"
#include "meshcompromise/bridge_log.h"

namespace meshcompromise
{

namespace
{
constexpr uint32_t kMinHoldMs = 20;
constexpr uint32_t kMinDwellMs = 20;
constexpr uint32_t kOverheadSanityMs = 500;
constexpr uint32_t kRederiveThresholdMs = 5;
constexpr uint32_t kMinTxYieldMs = 1000;
constexpr uint32_t kMaxTxYieldMs = 8000;
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

    const uint32_t previousHold = holdMs_;
    const uint32_t previousDwell = dwellMs_;
    const uint32_t previousMaxDwell = maxDwellMs_;

    detectMs_ = minimumDetectDwellMs(meshcoreProfile_);
    derivedForOverheadMs_ = switchOverheadMs_;

    const uint32_t preamble = static_cast<uint32_t>(preambleTimeMs(meshcoreProfile_));
    const uint32_t hostDetectMs = minimumDetectDwellMs(meshtasticProfile_);
    const uint32_t blindBudget = preamble > detectMs_ ? preamble - detectMs_ : 0;
    uint32_t autoHold = blindBudget > switchOverheadMs_ ? blindBudget - switchOverheadMs_ : 0;
    if (autoHold < hostDetectMs)
        autoHold = hostDetectMs;
    if (autoHold < kMinHoldMs)
        autoHold = kMinHoldMs;
    holdMs_ = config_.meshtasticHoldMs != 0 ? config_.meshtasticHoldMs : autoHold;

    uint32_t autoDwell = detectMs_ + detectMs_ / 2;
    if (autoDwell < kMinDwellMs)
        autoDwell = kMinDwellMs;
    dwellMs_ = config_.scanDwellMs != 0 ? config_.scanDwellMs : autoDwell;
    if (dwellMs_ < detectMs_)
        dwellMs_ = detectMs_;

    const uint32_t worstCase = static_cast<uint32_t>(packetAirtimeMs(meshcoreProfile_, 184)) + detectMs_;
    maxDwellMs_ = config_.maxDwellMs < worstCase ? worstCase : config_.maxDwellMs;

    txYieldBudgetMs_ = static_cast<uint32_t>(packetAirtimeMs(meshtasticProfile_, 233)) * 2;
    if (txYieldBudgetMs_ < kMinTxYieldMs)
        txYieldBudgetMs_ = kMinTxYieldMs;
    if (txYieldBudgetMs_ > kMaxTxYieldMs)
        txYieldBudgetMs_ = kMaxTxYieldMs;

    if (holdMs_ != previousHold || dwellMs_ != previousDwell || maxDwellMs_ != previousMaxDwell) {
        MC_LOG_INFO("timing: hold=%ums dwell=%ums detect=%u/%ums switch=%ums duty~%u%%", static_cast<unsigned>(holdMs_),
                    static_cast<unsigned>(dwellMs_), static_cast<unsigned>(hostDetectMs), static_cast<unsigned>(detectMs_),
                    static_cast<unsigned>(switchOverheadMs_),
                    static_cast<unsigned>((100 * dwellMs_) / std::max<uint32_t>(1, holdMs_ + dwellMs_)));

        if (holdMs_ + switchOverheadMs_ + detectMs_ > preamble)
            MC_LOG_WARN("blind %ums exceeds the %ums meshcore preamble, captures will be lossy",
                        static_cast<unsigned>(holdMs_ + switchOverheadMs_), static_cast<unsigned>(preamble));
    }
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
    return holdMs_;
}

uint32_t SliceScheduler::handleMeshtastic(uint32_t nowMs)
{
    if (!config_.enabled)
        return holdMs_;

    const uint32_t held = nowMs - stateEnteredMs_;
    if (held < holdMs_)
        return holdMs_ - held;

    if (ops_.meshtasticTxPending()) {
        if (!txYieldWaiting_) {
            txYieldWaiting_ = true;
            txYieldStartedMs_ = nowMs;
        }
        if (nowMs - txYieldStartedMs_ < txYieldBudgetMs_) {
            stats_.slicesYieldedTx++;
            return config_.busyRetryMs;
        }
    } else {
        txYieldWaiting_ = false;
    }

    if (ops_.meshtasticBusy()) {
        stats_.slicesSkippedBusy++;
        return config_.busyRetryMs;
    }

    beginSlice(nowMs);
    return dwellMs_;
}

uint32_t SliceScheduler::handleScan(uint32_t nowMs)
{
    const bool wanted = ops_.meshcoreTxPending() || ops_.meshcoreTxBusy();
    const bool active = ops_.meshcoreReceiving() || ops_.meshcorePacketInProgress() || ops_.channelActive();

    if (active || wanted) {
        if (active) {
            stats_.cadPositive++;
            consecutiveQuietSlices_ = 0;
        }
        state_ = SliceState::MeshcoreDwell;
        cycleExtended_ = true;
        stateEnteredMs_ = nowMs;
        dwellStartedMs_ = nowMs;
        ops_.pumpMeshcore();
        return config_.pumpIntervalMs;
    }

    stats_.cadNegative++;
    consecutiveQuietSlices_++;
    endSlice(nowMs);
    return holdMs_;
}

uint32_t SliceScheduler::handleDwell(uint32_t nowMs)
{
    ops_.pumpMeshcore();

    const bool busy = ops_.meshcoreReceiving() || ops_.meshcorePacketInProgress() || ops_.meshcoreTxBusy() ||
                      ops_.meshcoreTxPending();
    if (busy) {
        if (nowMs - dwellStartedMs_ >= maxDwellMs_) {
            stats_.dwellTimeouts++;
            endSlice(nowMs);
            return holdMs_;
        }
        return config_.pumpIntervalMs;
    }

    endSlice(nowMs);
    return holdMs_;
}

void SliceScheduler::beginSlice(uint32_t nowMs)
{
    if (sliceStartSeen_ && !cycleExtended_) {
        const uint32_t cycle = nowMs - lastSliceStartMs_;
        const uint32_t expected = holdMs_ + dwellMs_;
        const uint32_t excess = cycle > expected ? cycle - expected : 0;
        if (excess <= kOverheadSanityMs)
            switchOverheadMs_ = (switchOverheadMs_ * 3 + excess) / 4;
    }
    lastSliceStartMs_ = nowMs;
    sliceStartSeen_ = true;
    cycleExtended_ = false;

    const uint32_t drift = switchOverheadMs_ > derivedForOverheadMs_ ? switchOverheadMs_ - derivedForOverheadMs_
                                                                    : derivedForOverheadMs_ - switchOverheadMs_;
    if (drift >= kRederiveThresholdMs)
        applyDerivedTiming();

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
