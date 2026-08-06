#include "meshcompromise/settings.h"

namespace meshcompromise
{

bool validateProfile(const LoraProfile &profile, const SettingsBounds &bounds)
{
    if (profile.frequencyMhz < bounds.minFrequencyMhz || profile.frequencyMhz > bounds.maxFrequencyMhz)
        return false;
    if (profile.bandwidthKhz < bounds.minBandwidthKhz || profile.bandwidthKhz > bounds.maxBandwidthKhz)
        return false;
    if (profile.spreadingFactor < bounds.minSpreadingFactor || profile.spreadingFactor > bounds.maxSpreadingFactor)
        return false;
    if (profile.codingRate < bounds.minCodingRate || profile.codingRate > bounds.maxCodingRate)
        return false;
    return true;
}

bool validateSettings(const BridgeSettings &settings, const SettingsBounds &bounds)
{
    if (!validateProfile(settings.meshcore, bounds))
        return false;
    if (settings.hopLimit > bounds.maxHopLimit)
        return false;
    return true;
}

BridgeSettings defaultSettings()
{
    BridgeSettings settings;
    settings.meshcoreEnabled = true;
    settings.mirror.enabled = true;
    settings.mirror.policy = MirrorPolicy::LocalOnly;
    settings.slice.enabled = true;
    settings.slice.adaptive = true;
    normalizeSettings(settings);
    return settings;
}

void normalizeSettings(BridgeSettings &settings)
{
    settings.meshcore.preambleSymbols = meshcorePreambleSymbols(settings.meshcore.spreadingFactor);
    settings.slice.enabled = settings.meshcoreEnabled;
}

} // namespace meshcompromise
