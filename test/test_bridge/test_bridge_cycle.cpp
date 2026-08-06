#include <gtest/gtest.h>

#include "meshcompromise/bridge_cycle.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kBoot = 1000;

BridgeCycle startedCycle(uint16_t advertMinutes = 30, uint16_t statsMinutes = 5)
{
    BridgeCycle cycle;
    cycle.setIntervals(0, advertMinutes, statsMinutes);
    cycle.tick(kBoot);
    return cycle;
}

} // namespace

TEST(BridgeCycle, DoesNothingUntilTheFirstTick)
{
    BridgeCycle cycle;
    EXPECT_FALSE(cycle.started());
    EXPECT_EQ(0u, cycle.uptimeSeconds(999999));
}

TEST(BridgeCycle, TheFirstTickStartsEverythingAndRefreshesProfiles)
{
    BridgeCycle cycle;
    cycle.setIntervals(0, 30, 5);

    const CycleActions actions = cycle.tick(kBoot);

    EXPECT_TRUE(actions.firstTick);
    EXPECT_TRUE(actions.refreshProfiles);
    EXPECT_FALSE(actions.advertDue);
    EXPECT_FALSE(actions.statsDue);
    EXPECT_TRUE(cycle.started());
}

TEST(BridgeCycle, OnlyTheFirstTickReportsItself)
{
    BridgeCycle cycle = startedCycle();
    EXPECT_FALSE(cycle.tick(kBoot + 1).firstTick);
}

TEST(BridgeCycle, ProfilesAreRefreshedOnTheRefreshInterval)
{
    BridgeCycle cycle = startedCycle();

    EXPECT_FALSE(cycle.tick(kBoot + kProfileRefreshIntervalMs - 1).refreshProfiles);
    EXPECT_TRUE(cycle.tick(kBoot + kProfileRefreshIntervalMs).refreshProfiles);
    EXPECT_FALSE(cycle.tick(kBoot + kProfileRefreshIntervalMs + 1).refreshProfiles);
    EXPECT_TRUE(cycle.tick(kBoot + 2 * kProfileRefreshIntervalMs).refreshProfiles);
}

TEST(BridgeCycle, TheFirstAdvertWaitsForItsStartupDelay)
{
    BridgeCycle cycle = startedCycle();

    EXPECT_FALSE(cycle.tick(kBoot + kFirstAdvertDelayMs - 1).advertDue);
    EXPECT_TRUE(cycle.tick(kBoot + kFirstAdvertDelayMs).advertDue);
}

TEST(BridgeCycle, TheFirstStatsLineWaitsForItsStartupDelay)
{
    BridgeCycle cycle = startedCycle();

    EXPECT_FALSE(cycle.tick(kBoot + kFirstStatsDelayMs - 1).statsDue);
    EXPECT_TRUE(cycle.tick(kBoot + kFirstStatsDelayMs).statsDue);
}

TEST(BridgeCycle, AdvertsRepeatOnTheirInterval)
{
    BridgeCycle cycle = startedCycle(2, 0);

    ASSERT_TRUE(cycle.tick(kBoot + kFirstAdvertDelayMs).advertDue);

    const uint32_t next = kBoot + kFirstAdvertDelayMs + 2 * kMinuteMs;
    EXPECT_FALSE(cycle.tick(next - 1).advertDue);
    EXPECT_TRUE(cycle.tick(next).advertDue);
}

TEST(BridgeCycle, AZeroIntervalDisablesTheTimer)
{
    BridgeCycle cycle = startedCycle(0, 0);

    EXPECT_FALSE(cycle.tick(kBoot + kFirstAdvertDelayMs).advertDue);
    EXPECT_FALSE(cycle.tick(kBoot + kFirstStatsDelayMs).statsDue);
    EXPECT_FALSE(cycle.tick(kBoot + 10 * kMinuteMs).advertDue);
}

TEST(BridgeCycle, ChangingAnIntervalPushesTheNextFireBackToTheStartupDelay)
{
    BridgeCycle cycle = startedCycle(30, 5);

    const uint32_t change = kBoot + 1000;
    cycle.setIntervals(change, 10, 5);

    EXPECT_FALSE(cycle.tick(change + kFirstAdvertDelayMs - 1).advertDue);
    EXPECT_TRUE(cycle.tick(change + kFirstAdvertDelayMs).advertDue);
}

TEST(BridgeCycle, AnUnchangedIntervalLeavesTheTimerAlone)
{
    BridgeCycle cycle = startedCycle(30, 5);

    cycle.setIntervals(kBoot + 1000, 30, 5);

    EXPECT_TRUE(cycle.tick(kBoot + kFirstAdvertDelayMs).advertDue);
}

TEST(BridgeCycle, ChangingTheStatsIntervalPushesItsNextFireBack)
{
    BridgeCycle cycle = startedCycle(30, 5);

    const uint32_t change = kBoot + 1000;
    cycle.setIntervals(change, 30, 2);

    EXPECT_FALSE(cycle.tick(change + kFirstStatsDelayMs - 1).statsDue);
    EXPECT_TRUE(cycle.tick(change + kFirstStatsDelayMs).statsDue);
}

TEST(BridgeCycle, ChangingOneIntervalDoesNotDisturbTheOther)
{
    BridgeCycle cycle = startedCycle(30, 5);

    cycle.setIntervals(kBoot + 1000, 10, 5);

    EXPECT_TRUE(cycle.tick(kBoot + kFirstStatsDelayMs).statsDue);
}

TEST(BridgeCycle, IntervalsSetBeforeTheFirstTickAreNotRearmed)
{
    BridgeCycle cycle;
    cycle.setIntervals(0, 30, 5);
    cycle.setIntervals(0, 10, 3);

    cycle.tick(kBoot);

    EXPECT_TRUE(cycle.tick(kBoot + kFirstAdvertDelayMs).advertDue);
    EXPECT_EQ(10u, cycle.advertTimer().intervalMinutes());
    EXPECT_EQ(3u, cycle.statsTimer().intervalMinutes());
}

TEST(BridgeCycle, UptimeCountsFromTheFirstTick)
{
    BridgeCycle cycle = startedCycle();

    EXPECT_EQ(0u, cycle.tick(kBoot + 999).uptimeSeconds);
    EXPECT_EQ(1u, cycle.tick(kBoot + 1000).uptimeSeconds);
    EXPECT_EQ(90u, cycle.tick(kBoot + 90500).uptimeSeconds);
}

TEST(BridgeCycle, AZeroDelayNeverStallsTheScheduler)
{
    EXPECT_EQ(1u, BridgeCycle::clampDelay(0));
    EXPECT_EQ(1u, BridgeCycle::clampDelay(1));
    EXPECT_EQ(250u, BridgeCycle::clampDelay(250));
}

TEST(BridgeCycle, AdvertAndStatsCanFallDueOnTheSameTick)
{
    BridgeCycle cycle = startedCycle(1, 1);

    const uint32_t late = kBoot + kFirstStatsDelayMs;
    const CycleActions actions = cycle.tick(late);

    EXPECT_TRUE(actions.advertDue);
    EXPECT_TRUE(actions.statsDue);
}

TEST(BridgeCycle, TimeRunningBackwardsDoesNotWedgeTheTimers)
{
    BridgeCycle cycle = startedCycle(1, 1);

    ASSERT_TRUE(cycle.tick(kBoot + kFirstStatsDelayMs).statsDue);

    cycle.tick(kBoot);

    EXPECT_TRUE(cycle.tick(kBoot + kFirstStatsDelayMs + 2 * kMinuteMs).statsDue);
}
