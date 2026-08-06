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

int imageCalibrationBand(float frequencyMhz)
{
    if (frequencyMhz >= 430.0f && frequencyMhz <= 440.0f)
        return 1;
    if (frequencyMhz >= 470.0f && frequencyMhz <= 510.0f)
        return 2;
    if (frequencyMhz >= 779.0f && frequencyMhz <= 787.0f)
        return 3;
    if (frequencyMhz >= 863.0f && frequencyMhz <= 870.0f)
        return 4;
    if (frequencyMhz >= 902.0f && frequencyMhz <= 928.0f)
        return 5;
    return 0;
}

bool sameImageCalibrationBand(float aMhz, float bMhz)
{
    const int band = imageCalibrationBand(aMhz);
    return band != 0 && band == imageCalibrationBand(bMhz);
}

AlignmentBlocker alignmentBlocker(const LoraProfile &meshtastic, const LoraProfile &meshcore)
{
    if (meshtastic.spreadingFactor == 0 || meshcore.spreadingFactor == 0)
        return AlignmentBlocker::Unconfigured;
    if (meshtastic.spreadingFactor != meshcore.spreadingFactor)
        return AlignmentBlocker::SpreadingFactor;
    if (std::fabs(meshtastic.bandwidthKhz - meshcore.bandwidthKhz) >= kBandwidthEpsilonKhz)
        return AlignmentBlocker::Bandwidth;
    if (!frequenciesInterchangeable(meshtastic.frequencyMhz, meshcore.frequencyMhz, meshtastic.bandwidthKhz))
        return AlignmentBlocker::Frequency;
    return AlignmentBlocker::None;
}

SwitchMode selectSwitchMode(const LoraProfile &meshtastic, const LoraProfile &meshcore)
{
    return alignmentBlocker(meshtastic, meshcore) == AlignmentBlocker::None ? SwitchMode::Aligned : SwitchMode::Split;
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

LoraProfile meshcorePublicChannelProfile()
{
    // The real MeshCore "Public" channel's actual over-the-air parameters -
    // distinct from meshcoreDefaultProfile()'s SF8, which many tests pin
    // exact airtime numbers to and which does not match real MeshCore
    // hardware. Used as BridgeSettings::meshcore's default so a fresh
    // install can reach real MeshCore devices without any on-device
    // reconfiguration.
    LoraProfile profile;
    profile.frequencyMhz = 869.618f;
    profile.bandwidthKhz = 62.5f;
    profile.spreadingFactor = 7;
    profile.codingRate = 5;
    profile.syncWord = kMeshcoreSyncWord;
    profile.preambleSymbols = meshcorePreambleSymbols(profile.spreadingFactor);
    return profile;
}

LoraProfile meshcoreCardputerProfile()
{
    LoraProfile profile;
    profile.frequencyMhz = 869.525f;
    profile.bandwidthKhz = 250.0f;
    profile.spreadingFactor = 11;
    profile.codingRate = 5;
    profile.syncWord = kMeshcoreSyncWord;
    profile.preambleSymbols = meshcorePreambleSymbols(profile.spreadingFactor);
    return profile;
}

LoraProfile meshtasticLongFastProfile()
{
    LoraProfile profile;
    profile.frequencyMhz = 869.525f;
    profile.bandwidthKhz = 250.0f;
    profile.spreadingFactor = 11;
    profile.codingRate = 5;
    profile.syncWord = kMeshtasticSyncWord;
    profile.preambleSymbols = 16;
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
