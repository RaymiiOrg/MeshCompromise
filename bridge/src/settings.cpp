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
    if (settings.advertIntervalMinutes > bounds.maxAdvertIntervalMinutes)
        return false;
    if (settings.statsIntervalMinutes > bounds.maxStatsIntervalMinutes)
        return false;
    if (settings.slice.scanDwellMs < bounds.minScanDwellMs || settings.slice.scanDwellMs > bounds.maxScanDwellMs)
        return false;
    if (settings.slice.meshtasticHoldMs > bounds.maxMeshtasticHoldMs)
        return false;
    return true;
}

BridgeSettings defaultSettings()
{
    BridgeSettings settings;
    settings.meshcoreEnabled = true;
    settings.mirror.enabled = true;
    settings.mirror.reverseEnabled = true;
    settings.mirror.policy = MirrorPolicy::AllBroadcasts;
    settings.advertIntervalMinutes = 60;
    settings.statsIntervalMinutes = 5;
    settings.slice.enabled = true;
    settings.slice.adaptive = true;
    settings.slice.meshtasticHoldMs = 0;
    settings.slice.scanDwellMs = 0;
    normalizeSettings(settings);
    return settings;
}

void normalizeSettings(BridgeSettings &settings)
{
    settings.meshcore.preambleSymbols = meshcorePreambleSymbols(settings.meshcore.spreadingFactor);
    settings.slice.enabled = settings.meshcoreEnabled;
}

} // namespace meshcompromise
