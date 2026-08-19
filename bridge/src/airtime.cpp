#include "meshcompromise/airtime.h"

#include <algorithm>
#include <cmath>

namespace meshcompromise
{

namespace
{
constexpr float kLowDataRateThresholdMs = 16.0f;
constexpr float kPreambleSyncSymbols = 4.25f;
constexpr uint32_t kHeaderSymbols = 8;
constexpr float kScanPeriodSafetyFactor = 0.6f;
constexpr float kScanPeriodCeilingFactor = 0.8f;
constexpr uint8_t kDetectSymbols = 8;
constexpr uint32_t kMinScanPeriodMs = 10;
constexpr uint32_t kAlignedSwitchMs = 1;
constexpr uint32_t kSplitSwitchMs = 6;
} // namespace

float symbolTimeMs(float bandwidthKhz, uint8_t spreadingFactor)
{
    if (bandwidthKhz <= 0.0f || spreadingFactor == 0)
        return 0.0f;
    return static_cast<float>(1u << spreadingFactor) / bandwidthKhz;
}

bool lowDataRateOptimize(float bandwidthKhz, uint8_t spreadingFactor)
{
    return symbolTimeMs(bandwidthKhz, spreadingFactor) >= kLowDataRateThresholdMs;
}

float preambleTimeMs(const LoraProfile &profile)
{
    const float symbol = symbolTimeMs(profile.bandwidthKhz, profile.spreadingFactor);
    if (symbol <= 0.0f)
        return 0.0f;
    return (static_cast<float>(profile.preambleSymbols) + kPreambleSyncSymbols) * symbol;
}

uint32_t payloadSymbolCount(const LoraProfile &profile, uint16_t payloadBytes)
{
    const uint8_t sf = profile.spreadingFactor;
    if (sf == 0)
        return 0;

    const int de = lowDataRateOptimize(profile.bandwidthKhz, sf) ? 1 : 0;
    const int denominator = 4 * (static_cast<int>(sf) - 2 * de);
    if (denominator <= 0)
        return kHeaderSymbols;

    const int numerator = 8 * static_cast<int>(payloadBytes) - 4 * static_cast<int>(sf) + 28 + 16;
    const int codingSteps = static_cast<int>(std::ceil(static_cast<double>(numerator) / denominator));
    const int cr = std::max<int>(1, static_cast<int>(profile.codingRate) - 4);
    const int extra = std::max(codingSteps * (cr + 4), 0);
    return kHeaderSymbols + static_cast<uint32_t>(extra);
}

float packetAirtimeMs(const LoraProfile &profile, uint16_t payloadBytes)
{
    const float symbol = symbolTimeMs(profile.bandwidthKhz, profile.spreadingFactor);
    if (symbol <= 0.0f)
        return 0.0f;
    return preambleTimeMs(profile) + static_cast<float>(payloadSymbolCount(profile, payloadBytes)) * symbol;
}

uint32_t minimumDetectDwellMs(const LoraProfile &profile)
{
    const float symbol = symbolTimeMs(profile.bandwidthKhz, profile.spreadingFactor);
    if (symbol <= 0.0f)
        return 0;
    return static_cast<uint32_t>(std::ceil(symbol * kDetectSymbols));
}

uint32_t recommendedScanPeriodMs(const LoraProfile &profile)
{
    const float preamble = preambleTimeMs(profile);
    if (preamble <= 0.0f)
        return 0;
    const uint32_t period = static_cast<uint32_t>(preamble * kScanPeriodSafetyFactor);
    return std::max(period, kMinScanPeriodMs);
}

float packetScore(float snr, uint8_t spreadingFactor, int packetLength)
{
    static const float thresholds[] = {-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f};

    if (spreadingFactor < 7 || spreadingFactor > 12)
        return 0.0f;

    const float threshold = thresholds[spreadingFactor - 7];
    if (snr < threshold)
        return 0.0f;

    const float successRate = (snr - threshold) / 10.0f;
    const float collisionPenalty = 1.0f - (static_cast<float>(packetLength) / 256.0f);
    const float score = successRate * collisionPenalty;

    return std::max(0.0f, std::min(1.0f, score));
}

bool scanPeriodCoversPreamble(uint32_t periodMs, const LoraProfile &profile)
{
    const float preamble = preambleTimeMs(profile);
    if (preamble <= 0.0f)
        return false;
    return static_cast<float>(periodMs) + static_cast<float>(minimumDetectDwellMs(profile)) <= preamble;
}

uint32_t maxScanPeriodMs(const LoraProfile &profile)
{
    const float preamble = preambleTimeMs(profile);
    if (preamble <= 0.0f)
        return 0;

    const uint32_t budget = static_cast<uint32_t>(preamble * kScanPeriodCeilingFactor);
    const uint32_t detect = minimumDetectDwellMs(profile);
    if (budget <= detect)
        return 0;

    return std::max(budget - detect, recommendedScanPeriodMs(profile));
}

uint32_t scanExcursionMs(const LoraProfile &meshcore, SwitchMode mode)
{
    const uint32_t perSwitch = mode == SwitchMode::Aligned ? kAlignedSwitchMs : kSplitSwitchMs;
    return minimumDetectDwellMs(meshcore) + 2 * perSwitch;
}

bool hostToleratesScanning(const LoraProfile &meshtastic, const LoraProfile &meshcore, SwitchMode mode)
{
    const float hostPreamble = preambleTimeMs(meshtastic);
    if (hostPreamble <= 0.0f)
        return true;
    return hostPreamble > static_cast<float>(scanExcursionMs(meshcore, mode));
}

} // namespace meshcompromise
