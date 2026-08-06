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

TEST(Settings, DefaultProfileIsMeshcoreUpstream)
{
    const BridgeSettings settings = defaultSettings();
    EXPECT_FLOAT_EQ(settings.meshcore.frequencyMhz, 869.618f);
    EXPECT_EQ(settings.meshcore.spreadingFactor, 8);
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
