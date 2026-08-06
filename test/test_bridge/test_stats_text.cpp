#include <gtest/gtest.h>

#include <string>

#include "meshcompromise/mirror.h"
#include "meshcompromise/stats_text.h"

using namespace meshcompromise;

namespace
{

StatsSnapshot sample()
{
    StatsSnapshot stats;
    stats.uptimeSeconds = 754;
    stats.meshcoreHeard = 42;
    stats.meshcoreSent = 9;
    stats.mirroredOut = 7;
    stats.mirroredIn = 2;
    stats.adverts = 3;
    stats.meshcoreDutyCycle = 0.041f;
    stats.freeHeapBytes = 143360;
    stats.holdMs = 66;
    stats.switchOverheadMs = 4;
    return stats;
}

} // namespace

TEST(StatsText, ReportsBothDirectionsAndDutyCycle)
{
    char text[kStatsTextLength] = {0};
    const size_t length = buildStatsText(sample(), text, sizeof(text));

    ASSERT_GT(length, 0u);
    EXPECT_EQ(std::string(text), "up 12m rx42 tx9 mir7/2 adv3 duty4% hold66 sw4 heap140k");
}

TEST(StatsText, FitsBothProtocolsPayloadLimits)
{
    StatsSnapshot stats = sample();
    stats.uptimeSeconds = 99u * 86400u;
    stats.meshcoreHeard = 4000000000u;
    stats.meshcoreSent = 4000000000u;
    stats.mirroredOut = 4000000000u;
    stats.mirroredIn = 4000000000u;
    stats.adverts = 4000000000u;
    stats.freeHeapBytes = 8u * 1024u * 1024u;

    char text[kStatsTextLength] = {0};
    const size_t length = buildStatsText(stats, text, sizeof(text));

    EXPECT_GT(length, 0u);
    EXPECT_LT(length, kMeshcoreMaxText);
    EXPECT_LT(length, kMeshtasticMaxText);
}

TEST(StatsText, ShowsHoursAndDaysAsUptimeGrows)
{
    char text[kStatsTextLength] = {0};
    StatsSnapshot stats = sample();

    stats.uptimeSeconds = 3600 + 120;
    buildStatsText(stats, text, sizeof(text));
    EXPECT_EQ(std::string(text).compare(0, 8, "up 1h2m "), 0);

    stats.uptimeSeconds = 2 * 86400 + 3 * 3600;
    buildStatsText(stats, text, sizeof(text));
    EXPECT_EQ(std::string(text).compare(0, 8, "up 2d3h "), 0);
}

TEST(StatsText, RejectsAMissingBuffer)
{
    EXPECT_EQ(buildStatsText(sample(), nullptr, 16), 0u);
    char text[8] = {0};
    EXPECT_EQ(buildStatsText(sample(), text, 0), 0u);
}

TEST(Mirror, SuppressingAPacketDoesNotCountAsAnInjection)
{
    MirrorConfig config;
    Mirror mirror(config);
    mirror.setLocalNode(1);

    mirror.suppress(0x2222);

    EXPECT_EQ(mirror.injectedCount(), 0u);

    MirrorSource source;
    source.packetId = 0x2222;
    source.fromNode = 1;
    source.isTextMessage = true;
    source.isBroadcast = true;

    MirrorMessage message;
    EXPECT_EQ(mirror.prepare(source, "stats", 5, message), MirrorDecision::AlreadyMirrored);
}
