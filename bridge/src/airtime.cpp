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
constexpr uint8_t kDetectSymbols = 8;
constexpr uint32_t kMinScanPeriodMs = 10;
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

bool scanPeriodCoversPreamble(uint32_t periodMs, const LoraProfile &profile)
{
    const float preamble = preambleTimeMs(profile);
    if (preamble <= 0.0f)
        return false;
    return static_cast<float>(periodMs) + static_cast<float>(minimumDetectDwellMs(profile)) <= preamble;
}

} // namespace meshcompromise
