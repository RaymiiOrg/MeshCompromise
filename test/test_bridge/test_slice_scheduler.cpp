#include <gtest/gtest.h>

#include "fake_radio.h"
#include "meshcompromise/airtime.h"
#include "meshcompromise/slice_scheduler.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kRederiveThresholdMs = 5;

class SchedulerTest : public ::testing::Test
{
  protected:
    FakeRadio radio;
    SliceConfig config;
    uint32_t now = 0;

    SliceScheduler make()
    {
        SliceScheduler scheduler(radio, config);
        scheduler.setProfiles(meshtasticNarrowSlowProfile(), meshcoreDefaultProfile());
        return scheduler;
    }

    void advance(SliceScheduler &scheduler, uint32_t steps)
    {
        for (uint32_t i = 0; i < steps; i++) {
            const uint32_t delay = scheduler.tick(now);
            now += delay == 0 ? 1 : delay;
        }
    }
};

} // namespace

TEST_F(SchedulerTest, StartsOwnedByMeshtastic)
{
    SliceScheduler scheduler = make();
    EXPECT_EQ(scheduler.state(), SliceState::Meshtastic);
    EXPECT_EQ(radio.enterCount, 0);
}

TEST_F(SchedulerTest, DerivesHoldFromMeshcorePreamble)
{
    SliceScheduler scheduler = make();
    const LoraProfile meshcore = meshcoreDefaultProfile();
    EXPECT_TRUE(scanPeriodCoversPreamble(scheduler.holdMs(), meshcore));
}

TEST_F(SchedulerTest, HoldSpendsTheWholeBlindBudgetAndNoMore)
{
    SliceScheduler scheduler = make();
    const LoraProfile meshcore = meshcoreDefaultProfile();
    const uint32_t blind = scheduler.holdMs() + scheduler.switchOverheadMs() + minimumDetectDwellMs(meshcore);
    EXPECT_NEAR(static_cast<float>(blind), preambleTimeMs(meshcore), 1.0f);
    EXPECT_LE(static_cast<float>(blind), preambleTimeMs(meshcore));
}

TEST_F(SchedulerTest, HoldNeverDropsBelowTheMeshtasticDetectFloor)
{
    LoraProfile longFast;
    longFast.frequencyMhz = 869.525f;
    longFast.bandwidthKhz = 250.0f;
    longFast.spreadingFactor = 11;
    longFast.codingRate = 5;
    longFast.syncWord = 0x2b;
    longFast.preambleSymbols = 16;

    LoraProfile meshcore = meshcoreDefaultProfile();
    meshcore.spreadingFactor = 7;
    meshcore.preambleSymbols = meshcorePreambleSymbols(7);

    SliceScheduler scheduler(radio, config);
    scheduler.setProfiles(longFast, meshcore);

    EXPECT_GE(scheduler.holdMs(), minimumDetectDwellMs(longFast));
}

TEST_F(SchedulerTest, AnExplicitHoldOverridesTheDerivedOne)
{
    config.meshtasticHoldMs = 250;
    SliceScheduler scheduler = make();
    EXPECT_EQ(scheduler.holdMs(), 250u);
}

TEST_F(SchedulerTest, AnExplicitDwellStillCoversTheDetectFloor)
{
    config.scanDwellMs = 1;
    SliceScheduler scheduler = make();
    EXPECT_EQ(scheduler.dwellMs(), minimumDetectDwellMs(meshcoreDefaultProfile()));
}

TEST_F(SchedulerTest, AnExplicitDwellIsHonouredWhenItClearsTheFloor)
{
    config.scanDwellMs = 400;
    SliceScheduler scheduler = make();
    EXPECT_EQ(scheduler.dwellMs(), 400u);
}

TEST_F(SchedulerTest, PicksAlignedModeForNarrowSlow)
{
    SliceScheduler scheduler = make();
    EXPECT_EQ(scheduler.mode(), SwitchMode::Aligned);
}

TEST_F(SchedulerTest, PicksSplitModeForLongFast)
{
    LoraProfile longFast;
    longFast.frequencyMhz = 869.525f;
    longFast.bandwidthKhz = 250.0f;
    longFast.spreadingFactor = 11;
    longFast.codingRate = 5;
    longFast.syncWord = 0x2b;
    longFast.preambleSymbols = 16;

    SliceScheduler scheduler(radio, config);
    scheduler.setProfiles(longFast, meshcoreDefaultProfile());
    EXPECT_EQ(scheduler.mode(), SwitchMode::Split);
}

TEST_F(SchedulerTest, NeverTakesSliceWhileMeshtasticBusy)
{
    SliceScheduler scheduler = make();
    radio.busy = true;
    advance(scheduler, 200);
    EXPECT_EQ(radio.enterCount, 0);
    EXPECT_GT(scheduler.stats().slicesSkippedBusy, 0u);
    EXPECT_EQ(radio.busyDuringEnter, 0);
}

TEST_F(SchedulerTest, TakesSliceOnceMeshtasticGoesIdle)
{
    SliceScheduler scheduler = make();
    radio.busy = true;
    advance(scheduler, 20);
    EXPECT_EQ(radio.enterCount, 0);

    radio.busy = false;
    advance(scheduler, 5);
    EXPECT_GT(radio.enterCount, 0);
}

TEST_F(SchedulerTest, QuietChannelReturnsRadioOnTheVeryNextTick)
{
    SliceScheduler scheduler = make();
    radio.quiet();

    while (scheduler.state() != SliceState::MeshcoreScan) {
        const uint32_t delay = scheduler.tick(now);
        now += delay == 0 ? 1 : delay;
    }

    const int leavesBefore = radio.leaveCount;
    scheduler.tick(now);

    EXPECT_EQ(scheduler.state(), SliceState::Meshtastic);
    EXPECT_EQ(radio.leaveCount, leavesBefore + 1);
    EXPECT_EQ(radio.enterCount, radio.leaveCount);
    EXPECT_GT(scheduler.stats().cadNegative, 0u);
}

TEST_F(SchedulerTest, RadioOwnershipIsNeverDoubleClaimed)
{
    SliceScheduler scheduler = make();
    for (int i = 0; i < 300; i++) {
        radio.busy = (i % 7) == 0;
        radio.cadActive = (i % 5) == 0;
        radio.receiving = (i % 11) == 0;
        const uint32_t delay = scheduler.tick(now);
        now += delay == 0 ? 1 : delay;
    }
    EXPECT_EQ(radio.ownershipViolations, 0);
}

TEST_F(SchedulerTest, ActiveChannelEntersDwellAndPumps)
{
    SliceScheduler scheduler = make();
    radio.cadActive = true;
    radio.receiving = true;
    advance(scheduler, 10);
    EXPECT_EQ(scheduler.state(), SliceState::MeshcoreDwell);
    EXPECT_GT(radio.pumpCount, 0);
    EXPECT_GT(scheduler.stats().cadPositive, 0u);
}

TEST_F(SchedulerTest, DwellIsNotTruncatedWhileReceiving)
{
    SliceScheduler scheduler = make();
    radio.cadActive = true;
    radio.receiving = true;
    advance(scheduler, 30);
    EXPECT_EQ(radio.leaveCount, 0);
    EXPECT_EQ(scheduler.state(), SliceState::MeshcoreDwell);
}

TEST_F(SchedulerTest, DwellEndsWhenReceiveCompletes)
{
    SliceScheduler scheduler = make();
    radio.cadActive = true;
    radio.receiving = true;
    advance(scheduler, 10);
    ASSERT_EQ(scheduler.state(), SliceState::MeshcoreDwell);

    radio.quiet();
    advance(scheduler, 3);
    EXPECT_EQ(scheduler.state(), SliceState::Meshtastic);
    EXPECT_EQ(radio.enterCount, radio.leaveCount);
}

TEST_F(SchedulerTest, StuckReceiveHitsDwellTimeout)
{
    config.maxDwellMs = 50;
    SliceScheduler scheduler = make();
    radio.cadActive = true;
    radio.receiving = true;
    advance(scheduler, 4000);
    EXPECT_GT(scheduler.stats().dwellTimeouts, 0u);
    EXPECT_EQ(radio.ownershipViolations, 0);
}

TEST_F(SchedulerTest, PendingTransmitWinsSliceEvenWhenChannelQuiet)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    radio.txPending = true;
    advance(scheduler, 10);
    EXPECT_EQ(scheduler.state(), SliceState::MeshcoreDwell);
    EXPECT_GT(radio.pumpCount, 0);
}

TEST_F(SchedulerTest, DisabledSchedulerNeverTouchesRadio)
{
    config.enabled = false;
    SliceScheduler scheduler = make();
    radio.cadActive = true;
    advance(scheduler, 200);
    EXPECT_EQ(radio.enterCount, 0);
    EXPECT_EQ(radio.pumpCount, 0);
}

TEST_F(SchedulerTest, RuntimeDisableStopsFurtherSlices)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    advance(scheduler, 20);
    ASSERT_GT(radio.enterCount, 0);

    scheduler.setEnabled(false);
    const int taken = radio.enterCount;
    advance(scheduler, 200);
    EXPECT_EQ(radio.enterCount, taken);
}

TEST_F(SchedulerTest, ALongQuietRunStillReactsToTheFirstArrival)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    advance(scheduler, 400);

    const uint32_t quietSlices = scheduler.stats().cadNegative;
    scheduler.resetStats();

    radio.cadActive = true;
    radio.receiving = true;
    advance(scheduler, 20);
    EXPECT_GT(quietSlices, 0u);
    EXPECT_EQ(scheduler.state(), SliceState::MeshcoreDwell);
}

TEST_F(SchedulerTest, AQuietRunDoesNotWidenTheHold)
{
    SliceScheduler scheduler = make();
    const uint32_t derived = scheduler.holdMs();
    radio.quiet();
    advance(scheduler, 400);
    EXPECT_EQ(scheduler.holdMs(), derived);
}

TEST_F(SchedulerTest, APendingMeshtasticTransmitDefersTheSlice)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    radio.hostTxPending = true;
    advance(scheduler, 100);

    EXPECT_EQ(radio.enterCount, 0);
    EXPECT_EQ(scheduler.state(), SliceState::Meshtastic);
    EXPECT_GT(scheduler.stats().slicesYieldedTx, 0u);
}

TEST_F(SchedulerTest, TheSliceResumesOnceTheTransmitQueueDrains)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    radio.hostTxPending = true;
    advance(scheduler, 100);
    ASSERT_EQ(radio.enterCount, 0);

    radio.hostTxPending = false;
    advance(scheduler, 5);
    EXPECT_GT(radio.enterCount, 0);
}

TEST_F(SchedulerTest, AWedgedTransmitQueueCannotStarveMeshcoreForever)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    radio.hostTxPending = true;
    advance(scheduler, 4000);

    EXPECT_GT(radio.enterCount, 0);
    EXPECT_GT(scheduler.stats().slicesYieldedTx, 0u);
}

TEST_F(SchedulerTest, AnIdealCycleMeasuresNoSwitchOverhead)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    advance(scheduler, 40);
    EXPECT_EQ(scheduler.switchOverheadMs(), 0u);
}

TEST_F(SchedulerTest, RealSwitchCostIsMeasuredAndGivenBackFromTheHold)
{
    SliceScheduler scheduler = make();
    const uint32_t derived = scheduler.holdMs();
    radio.quiet();

    for (int i = 0; i < 400; i++) {
        const uint32_t delay = scheduler.tick(now);
        now += (delay == 0 ? 1 : delay) + 20;
    }

    EXPECT_GT(scheduler.switchOverheadMs(), 0u);
    EXPECT_LT(scheduler.holdMs(), derived);

    const uint32_t blind = scheduler.holdMs() + scheduler.switchOverheadMs();
    EXPECT_LE(blind, derived + kRederiveThresholdMs);
    EXPECT_GE(blind + kRederiveThresholdMs, derived);
}

TEST_F(SchedulerTest, AnAbsurdCycleGapIsNotMistakenForSwitchOverhead)
{
    SliceScheduler scheduler = make();
    radio.quiet();

    for (int i = 0; i < 40; i++) {
        const uint32_t delay = scheduler.tick(now);
        now += (delay == 0 ? 1 : delay) + 5000;
    }

    EXPECT_EQ(scheduler.switchOverheadMs(), 0u);
}

TEST_F(SchedulerTest, MeshtasticKeepsMajorityOfAirtimeWhenQuiet)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    advance(scheduler, 500);
    EXPECT_LT(scheduler.stats().meshcoreDutyCycle(), 0.5f);
    EXPECT_GT(scheduler.stats().meshtasticListenMs, scheduler.stats().meshcoreListenMs);
}

TEST_F(SchedulerTest, DutyCycleIsZeroBeforeAnyTime)
{
    SliceScheduler scheduler = make();
    EXPECT_FLOAT_EQ(scheduler.stats().meshcoreDutyCycle(), 0.0f);
}

TEST_F(SchedulerTest, EnterUsesSelectedMode)
{
    SliceScheduler scheduler = make();
    radio.quiet();
    advance(scheduler, 20);
    ASSERT_FALSE(radio.enterModes.empty());
    EXPECT_EQ(radio.enterModes.front(), SwitchMode::Aligned);
}
