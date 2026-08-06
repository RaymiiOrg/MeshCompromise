#include "meshcompromise/lora_profile.h"

#include <cmath>

namespace meshcompromise
{

namespace
{
constexpr float kFrequencyEpsilonMhz = 0.000001f;
constexpr float kBandwidthEpsilonKhz = 0.001f;
constexpr float kToleranceFraction = 0.25f;
constexpr uint8_t kMeshtasticSyncWord = 0x2b;
constexpr uint8_t kMeshcoreSyncWord = 0x12;
} // namespace

bool operator==(const LoraProfile &a, const LoraProfile &b)
{
    return std::fabs(a.frequencyMhz - b.frequencyMhz) < kFrequencyEpsilonMhz &&
           std::fabs(a.bandwidthKhz - b.bandwidthKhz) < kBandwidthEpsilonKhz &&
           a.spreadingFactor == b.spreadingFactor && a.codingRate == b.codingRate && a.syncWord == b.syncWord &&
           a.preambleSymbols == b.preambleSymbols;
}

bool operator!=(const LoraProfile &a, const LoraProfile &b)
{
    return !(a == b);
}

float frequencyToleranceKhz(float bandwidthKhz)
{
    if (bandwidthKhz <= 0.0f)
        return 0.0f;
    return bandwidthKhz * kToleranceFraction;
}

bool frequenciesInterchangeable(float aMhz, float bMhz, float bandwidthKhz)
{
    const float toleranceKhz = frequencyToleranceKhz(bandwidthKhz);
    if (toleranceKhz <= 0.0f)
        return false;
    const float deltaKhz = std::fabs(aMhz - bMhz) * 1000.0f;
    return deltaKhz <= toleranceKhz;
}

SwitchMode selectSwitchMode(const LoraProfile &meshtastic, const LoraProfile &meshcore)
{
    if (meshtastic.spreadingFactor == 0 || meshcore.spreadingFactor == 0)
        return SwitchMode::Split;
    if (meshtastic.spreadingFactor != meshcore.spreadingFactor)
        return SwitchMode::Split;
    if (std::fabs(meshtastic.bandwidthKhz - meshcore.bandwidthKhz) >= kBandwidthEpsilonKhz)
        return SwitchMode::Split;
    if (!frequenciesInterchangeable(meshtastic.frequencyMhz, meshcore.frequencyMhz, meshtastic.bandwidthKhz))
        return SwitchMode::Split;
    return SwitchMode::Aligned;
}

uint16_t meshcorePreambleSymbols(uint8_t spreadingFactor)
{
    return spreadingFactor <= 8 ? 32 : 16;
}

LoraProfile meshcoreDefaultProfile()
{
    LoraProfile profile;
    profile.frequencyMhz = 869.618f;
    profile.bandwidthKhz = 62.5f;
    profile.spreadingFactor = 8;
    profile.codingRate = 5;
    profile.syncWord = kMeshcoreSyncWord;
    profile.preambleSymbols = meshcorePreambleSymbols(profile.spreadingFactor);
    return profile;
}

LoraProfile meshtasticNarrowSlowProfile()
{
    LoraProfile profile;
    profile.frequencyMhz = 869.60825f;
    profile.bandwidthKhz = 62.5f;
    profile.spreadingFactor = 8;
    profile.codingRate = 6;
    profile.syncWord = kMeshtasticSyncWord;
    profile.preambleSymbols = 16;
    return profile;
}

} // namespace meshcompromise
