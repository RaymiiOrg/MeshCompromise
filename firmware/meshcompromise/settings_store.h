#pragma once

#include "meshcompromise/settings.h"

namespace meshcompromise
{

constexpr uint32_t kSettingsMagic = 0x4D435031;
constexpr uint16_t kSettingsVersion = 3;

struct StoredSettings {
    uint32_t magic = kSettingsMagic;
    uint16_t version = kSettingsVersion;
    uint16_t size = sizeof(BridgeSettings);
    BridgeSettings settings;
};

bool loadSettings(BridgeSettings &out);

bool saveSettings(const BridgeSettings &settings);

} // namespace meshcompromise
