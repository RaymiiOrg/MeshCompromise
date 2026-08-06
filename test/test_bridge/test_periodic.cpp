#include <gtest/gtest.h>

#include "meshcompromise/periodic.h"

using namespace meshcompromise;

namespace
{

PeriodicTimer minuteTimer(uint32_t now, uint32_t firstDelayMs, uint16_t minutes = 1)
{
    PeriodicTimer timer;
    timer.setIntervalMinutes(minutes);
    timer.start(now, firstDelayMs);
    return timer;
}

} // namespace

TEST(Periodic, AnUnstartedTimerIsNeverDue)
{
    PeriodicTimer timer;
    timer.setIntervalMinutes(1);

    EXPECT_FALSE(timer.armed());
    EXPECT_FALSE(timer.due(0));
    EXPECT_FALSE(timer.due(3600000));
}

TEST(Periodic, AZeroIntervalDisablesTheTimer)
{
    PeriodicTimer timer;
    timer.setIntervalMinutes(0);
    timer.start(1000, 0);

    EXPECT_FALSE(timer.enabled());
    EXPECT_FALSE(timer.due(1000));
    EXPECT_FALSE(timer.due(1000000));
}

TEST(Periodic, TheFirstFireWaitsForTheStartDelay)
{
    PeriodicTimer timer = minuteTimer(1000, 30000);

    EXPECT_FALSE(timer.due(1000));
    EXPECT_FALSE(timer.due(30999));
    EXPECT_TRUE(timer.due(31000));
}

TEST(Periodic, AfterFiringItRearmsOneIntervalLater)
{
    PeriodicTimer timer = minuteTimer(0, 0);

    EXPECT_TRUE(timer.due(0));
    EXPECT_FALSE(timer.due(59999));
    EXPECT_TRUE(timer.due(60000));
    EXPECT_FALSE(timer.due(60001));
    EXPECT_TRUE(timer.due(120000));
}

TEST(Periodic, TheIntervalIsCountedInMinutes)
{
    PeriodicTimer timer = minuteTimer(0, 0, 5);

    EXPECT_TRUE(timer.due(0));
    EXPECT_FALSE(timer.due(299999));
    EXPECT_TRUE(timer.due(300000));
}

TEST(Periodic, ALateTickStillFiresOnlyOnce)
{
    PeriodicTimer timer = minuteTimer(0, 0);

    EXPECT_TRUE(timer.due(0));
    EXPECT_TRUE(timer.due(600000));
    EXPECT_FALSE(timer.due(600001));
}

TEST(Periodic, ItSurvivesTheMillisecondWraparound)
{
    const uint32_t nearTop = 0xFFFFF000u;
    PeriodicTimer timer = minuteTimer(nearTop, 8192);

    EXPECT_FALSE(timer.due(nearTop + 8191));
    EXPECT_TRUE(timer.due(nearTop + 8192));
    EXPECT_FALSE(timer.due(nearTop + 8192 + 59999));
    EXPECT_TRUE(timer.due(nearTop + 8192 + 60000));
}

TEST(Periodic, RearmingPushesTheNextFireOut)
{
    PeriodicTimer timer = minuteTimer(0, 0);

    EXPECT_TRUE(timer.due(0));
    EXPECT_EQ(60000u, timer.nextDueMs());

    timer.rearm(1000, 30000);

    EXPECT_EQ(31000u, timer.nextDueMs());
    EXPECT_FALSE(timer.due(30999));
    EXPECT_TRUE(timer.due(31000));
}

TEST(Periodic, DisablingAfterStartStopsFurtherFires)
{
    PeriodicTimer timer = minuteTimer(0, 0);

    EXPECT_TRUE(timer.due(0));
    timer.setIntervalMinutes(0);

    EXPECT_FALSE(timer.due(60000));
    EXPECT_FALSE(timer.due(600000));
}

TEST(Periodic, ReenablingResumesFromTheStoredDeadline)
{
    PeriodicTimer timer = minuteTimer(0, 0);

    EXPECT_TRUE(timer.due(0));
    timer.setIntervalMinutes(0);
    EXPECT_FALSE(timer.due(60000));

    timer.setIntervalMinutes(1);
    EXPECT_TRUE(timer.due(60000));
}

TEST(Periodic, TheNextDeadlineIsVisible)
{
    PeriodicTimer timer = minuteTimer(5000, 20000);

    EXPECT_EQ(25000u, timer.nextDueMs());
    EXPECT_TRUE(timer.due(25000));
    EXPECT_EQ(85000u, timer.nextDueMs());
}
