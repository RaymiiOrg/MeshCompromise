#pragma once

#include <cstdint>

#include "meshcompromise/lora_profile.h"
#include "meshcompromise/mirror.h"
#include "meshcompromise/slice_scheduler.h"

namespace meshcompromise
{

struct BridgeSettings {
    bool meshcoreEnabled = true;
    LoraProfile meshcore = meshcorePublicChannelProfile();
    uint8_t hopLimit = 3;
    int8_t txPowerDbm = 22;
    uint16_t advertIntervalMinutes = 60;
    uint16_t statsIntervalMinutes = 1;
    MirrorConfig mirror;
    SliceConfig slice;
};

struct SettingsBounds {
    float minFrequencyMhz = 150.0f;
    float maxFrequencyMhz = 2500.0f;
    float minBandwidthKhz = 7.0f;
    float maxBandwidthKhz = 500.0f;
    uint8_t minSpreadingFactor = 5;
    uint8_t maxSpreadingFactor = 12;
    uint8_t minCodingRate = 5;
    uint8_t maxCodingRate = 8;
    uint8_t maxHopLimit = 7;
    uint16_t maxAdvertIntervalMinutes = 240;
    uint16_t maxStatsIntervalMinutes = 240;
    uint32_t minScanDwellMs = 0;
    uint32_t maxScanDwellMs = 2000;
    uint32_t maxMeshtasticHoldMs = 2000;
};

bool validateProfile(const LoraProfile &profile, const SettingsBounds &bounds = SettingsBounds());

bool validateSettings(const BridgeSettings &settings, const SettingsBounds &bounds = SettingsBounds());

BridgeSettings defaultSettings();

void normalizeSettings(BridgeSettings &settings);

} // namespace meshcompromise
