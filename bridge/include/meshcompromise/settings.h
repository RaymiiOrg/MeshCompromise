#pragma once

#include <cstdint>

#include "meshcompromise/lora_profile.h"
#include "meshcompromise/mirror.h"
#include "meshcompromise/slice_scheduler.h"

namespace meshcompromise
{

struct BridgeSettings {
    bool meshcoreEnabled = true;
    LoraProfile meshcore = meshcoreDefaultProfile();
    uint8_t hopLimit = 3;
    int8_t txPowerDbm = 20;
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
};

bool validateProfile(const LoraProfile &profile, const SettingsBounds &bounds = SettingsBounds());

bool validateSettings(const BridgeSettings &settings, const SettingsBounds &bounds = SettingsBounds());

BridgeSettings defaultSettings();

void normalizeSettings(BridgeSettings &settings);

} // namespace meshcompromise
