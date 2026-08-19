#include <gtest/gtest.h>

#include "meshcompromise/settings.h"

using namespace meshcompromise;

TEST(Settings, DualReceiveAndMirroringAreOnByDefault)
{
    const BridgeSettings settings = defaultSettings();
    EXPECT_TRUE(settings.meshcoreEnabled);
    EXPECT_TRUE(settings.slice.enabled);
    EXPECT_TRUE(settings.mirror.enabled);
}

TEST(Settings, DefaultsAreValid)
{
    EXPECT_TRUE(validateSettings(defaultSettings()));
}

TEST(Settings, ReverseMirroringAndAdvertsAreOnByDefault)
{
    const BridgeSettings settings = defaultSettings();
    EXPECT_TRUE(settings.mirror.reverseEnabled);
    EXPECT_EQ(settings.advertIntervalMinutes, 60);
}

TEST(Settings, AdvertIntervalOutOfRangeIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.advertIntervalMinutes = 1000;
    EXPECT_FALSE(validateSettings(settings));
}

TEST(Settings, AdvertsCanBeDisabled)
{
    BridgeSettings settings = defaultSettings();
    settings.advertIntervalMinutes = 0;
    EXPECT_TRUE(validateSettings(settings));
}

TEST(Settings, DefaultProfileMatchesRealMeshcorePublicChannel)
{
    // Not meshcoreCardputerProfile() (SF11/250kHz, aligned to Meshtastic
    // LongFast) - that PHY doesn't match any real MeshCore hardware, so a
    // bridge shipped with it can never be heard by an actual MeshCore node.
    const BridgeSettings settings = defaultSettings();
    EXPECT_FLOAT_EQ(settings.meshcore.frequencyMhz, 869.618f);
    EXPECT_FLOAT_EQ(settings.meshcore.bandwidthKhz, 62.5f);
    EXPECT_EQ(settings.meshcore.spreadingFactor, 7);
    EXPECT_EQ(settings.meshcore.codingRate, 5);
    EXPECT_EQ(settings.txPowerDbm, 22);
}

TEST(Settings, FrequencyOutOfRangeIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcore.frequencyMhz = 100.0f;
    EXPECT_FALSE(validateSettings(settings));
}

TEST(Settings, SpreadingFactorOutOfRangeIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcore.spreadingFactor = 13;
    EXPECT_FALSE(validateSettings(settings));
}

TEST(Settings, CodingRateOutOfRangeIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcore.codingRate = 9;
    EXPECT_FALSE(validateSettings(settings));
}

TEST(Settings, BandwidthOutOfRangeIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcore.bandwidthKhz = 1000.0f;
    EXPECT_FALSE(validateSettings(settings));
}

TEST(Settings, HopLimitAboveSevenIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.hopLimit = 8;
    EXPECT_FALSE(validateSettings(settings));
}

TEST(Settings, NormalizeDerivesPreambleFromSpreadingFactor)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcore.spreadingFactor = 11;
    normalizeSettings(settings);
    EXPECT_EQ(settings.meshcore.preambleSymbols, 16);

    settings.meshcore.spreadingFactor = 7;
    normalizeSettings(settings);
    EXPECT_EQ(settings.meshcore.preambleSymbols, 32);
}

TEST(Settings, DisablingMeshcoreDisablesSlicing)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcoreEnabled = false;
    normalizeSettings(settings);
    EXPECT_FALSE(settings.slice.enabled);
}

TEST(Settings, StatisticsBroadcastIsOnEveryFiveMinutesByDefault)
{
    EXPECT_EQ(defaultSettings().statsIntervalMinutes, 5);
}

TEST(Settings, StatsIntervalOutOfRangeIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.statsIntervalMinutes = 1000;
    EXPECT_FALSE(validateSettings(settings));
}

TEST(Settings, TimingsDefaultToDerived)
{
    const BridgeSettings settings = defaultSettings();
    EXPECT_EQ(settings.slice.meshtasticHoldMs, 0u);
    EXPECT_EQ(settings.slice.scanDwellMs, 0u);
}

TEST(Settings, HoldOutOfRangeIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.slice.meshtasticHoldMs = 5000;
    EXPECT_FALSE(validateSettings(settings));
}

TEST(Settings, HoldAtTheUpperBoundIsAccepted)
{
    BridgeSettings settings = defaultSettings();
    settings.slice.meshtasticHoldMs = 2000;
    EXPECT_TRUE(validateSettings(settings));
}

TEST(Settings, DwellOutOfRangeIsRejected)
{
    BridgeSettings settings = defaultSettings();
    settings.slice.scanDwellMs = 5000;
    EXPECT_FALSE(validateSettings(settings));
}
