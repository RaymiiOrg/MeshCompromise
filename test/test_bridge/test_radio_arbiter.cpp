#include <gtest/gtest.h>

#include "fake_sx_driver.h"
#include "meshcompromise/radio_arbiter.h"

using namespace meshcompromise;

namespace
{

class ArbiterTest : public ::testing::Test
{
  protected:
    FakeSxDriver driver;
    FakeHostRadio host;
    RadioArbiter arbiter{driver, host};

    LoraProfile meshcore = meshcoreDefaultProfile();
    LoraProfile meshtastic = meshtasticNarrowSlowProfile();
};

} // namespace

TEST_F(ArbiterTest, AlignedEntryOnlyWritesSyncWordAndPreamble)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);

    EXPECT_TRUE(driver.didOp("setSyncWord"));
    EXPECT_FALSE(driver.didOp("configure"));
    EXPECT_EQ(driver.syncWord, meshcore.syncWord);
}

TEST_F(ArbiterTest, AnUnprimedDriverGetsAFullConfigureEvenInAlignedMode)
{
    driver.primedOverride = false;

    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);

    EXPECT_TRUE(driver.didOp("configure"));
    ASSERT_EQ(driver.configured.size(), 1u);
    EXPECT_EQ(driver.configured.front().syncWord, meshcore.syncWord);
    EXPECT_TRUE(driver.primed());

    arbiter.leaveMeshcore(SwitchMode::Aligned, meshtastic);
    driver.clearOps();

    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);

    EXPECT_TRUE(driver.didOp("setSyncWord"));
    EXPECT_FALSE(driver.didOp("configure"));
}

TEST_F(ArbiterTest, SplitEntryReprogramsTheWholeModem)
{
    LoraProfile longFast;
    longFast.frequencyMhz = 869.525f;
    longFast.bandwidthKhz = 250.0f;
    longFast.spreadingFactor = 11;
    longFast.codingRate = 5;
    longFast.syncWord = 0x2b;
    longFast.preambleSymbols = 16;

    arbiter.enterMeshcore(SwitchMode::Split, meshcore);

    EXPECT_TRUE(driver.didOp("configure"));
    ASSERT_EQ(driver.configured.size(), 1u);
    EXPECT_FLOAT_EQ(driver.configured.front().frequencyMhz, meshcore.frequencyMhz);
    EXPECT_EQ(driver.configured.front().spreadingFactor, meshcore.spreadingFactor);
}

TEST_F(ArbiterTest, StayingInOneCalibrationBandRetunesInsteadOfReconfiguring)
{
    ASSERT_TRUE(sameImageCalibrationBand(host.profile.frequencyMhz, meshcore.frequencyMhz));

    arbiter.enterMeshcore(SwitchMode::Split, meshcore);

    EXPECT_EQ(driver.retunes, 1);
    EXPECT_TRUE(driver.didOp("retune"));
}

TEST_F(ArbiterTest, CrossingACalibrationBandForcesAFullConfigure)
{
    host.profile.frequencyMhz = 915.0f;
    ASSERT_FALSE(sameImageCalibrationBand(host.profile.frequencyMhz, meshcore.frequencyMhz));

    arbiter.enterMeshcore(SwitchMode::Split, meshcore);

    EXPECT_EQ(driver.retunes, 0);
    EXPECT_TRUE(driver.didOp("configure"));
}

TEST_F(ArbiterTest, AnUncalibratableHostFrequencyForcesAFullConfigure)
{
    host.profile.frequencyMhz = 0.0f;

    arbiter.enterMeshcore(SwitchMode::Split, meshcore);

    EXPECT_EQ(driver.retunes, 0);
    EXPECT_TRUE(driver.didOp("configure"));
}

TEST_F(ArbiterTest, MeshtasticTxPendingComesFromTheHost)
{
    EXPECT_FALSE(arbiter.meshtasticTxPending());

    host.pendingTx = true;
    EXPECT_TRUE(arbiter.meshtasticTxPending());

    host.pendingTx = false;
    EXPECT_FALSE(arbiter.meshtasticTxPending());
}

TEST_F(ArbiterTest, EntryAlwaysGoesToStandbyFirst)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    ASSERT_FALSE(driver.ops.empty());
    EXPECT_EQ(driver.ops.front(), "standby");
}

TEST_F(ArbiterTest, EntryArmsReceive)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    EXPECT_TRUE(driver.didOp("attachRxIrq"));
    EXPECT_TRUE(driver.didOp("startReceive"));
}

TEST_F(ArbiterTest, LeaseIsHeldOnlyBetweenEnterAndLeave)
{
    EXPECT_FALSE(arbiter.leaseHeld());
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    EXPECT_TRUE(arbiter.leaseHeld());
    arbiter.leaveMeshcore(SwitchMode::Aligned, meshtastic);
    EXPECT_FALSE(arbiter.leaseHeld());
}

TEST_F(ArbiterTest, AlignedLeaveOnlyWritesSyncWordAndPreamble)
{
    // Mirrors AlignedEntryOnlyWritesSyncWordAndPreamble: in Aligned mode only
    // the sync word and preamble differ from Meshtastic's settings, so
    // leaving should be as cheap as entering - no full host_.restore(), which
    // would otherwise force a RadioLibInterface reconfigure() (with its own
    // verbose logging) on every single handback.
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.clearOps();
    arbiter.leaveMeshcore(SwitchMode::Aligned, meshtastic);

    EXPECT_TRUE(driver.didOp("detachIrq"));
    EXPECT_TRUE(driver.didOp("setSyncWord"));
    EXPECT_EQ(driver.syncWord, meshtastic.syncWord);
    EXPECT_EQ(host.restores, 0);
}

TEST_F(ArbiterTest, SplitLeaveStillFullyRestoresTheHost)
{
    // Split mode changes real radio parameters (not just sync word/preamble),
    // so leaving still needs the host's full restore.
    arbiter.enterMeshcore(SwitchMode::Split, meshcore);
    arbiter.leaveMeshcore(SwitchMode::Split, meshtastic);

    EXPECT_TRUE(driver.didOp("detachIrq"));
    EXPECT_EQ(host.restores, 1);
}

TEST_F(ArbiterTest, AnUnprimedDriverGetsAFullRestoreEvenInAlignedMode)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.primedOverride = false;
    arbiter.leaveMeshcore(SwitchMode::Aligned, meshtastic);

    EXPECT_EQ(host.restores, 1);
}

TEST_F(ArbiterTest, DoubleEnterIsIgnored)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.clearOps();
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    EXPECT_TRUE(driver.ops.empty());
}

TEST_F(ArbiterTest, LeaveWithoutEnterTouchesNothing)
{
    arbiter.leaveMeshcore(SwitchMode::Aligned, meshtastic);
    EXPECT_TRUE(driver.ops.empty());
    EXPECT_EQ(host.restores, 0);
}

TEST_F(ArbiterTest, FailedConfigureReleasesTheLeaseAndRestores)
{
    driver.configureFails = true;
    arbiter.enterMeshcore(SwitchMode::Split, meshcore);

    EXPECT_FALSE(arbiter.leaseHeld());
    EXPECT_EQ(host.restores, 1);
}

TEST_F(ArbiterTest, MeshtasticBusyCoversSendAndReceive)
{
    EXPECT_FALSE(arbiter.meshtasticBusy());

    host.sending = true;
    EXPECT_TRUE(arbiter.meshtasticBusy());
    host.sending = false;

    host.receiving = true;
    EXPECT_TRUE(arbiter.meshtasticBusy());
    host.receiving = false;

    EXPECT_FALSE(arbiter.meshtasticBusy());
}

TEST_F(ArbiterTest, NoRegisterIsTouchedOutsideALease)
{
    arbiter.channelActive();
    arbiter.pumpMeshcore();
    EXPECT_TRUE(driver.ops.empty());
}

TEST_F(ArbiterTest, ReceivedFrameIsHandedUpVerbatim)
{
    const std::vector<uint8_t> frame = {0x15, 0x00, 0xAB, 0xCD, 0xEF};

    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.deliver(frame);
    arbiter.pumpMeshcore();

    ASSERT_TRUE(arbiter.rxAvailable());
    uint8_t out[64] = {0};
    const size_t length = arbiter.takeRx(out, sizeof(out));

    ASSERT_EQ(length, frame.size());
    EXPECT_EQ(std::vector<uint8_t>(out, out + length), frame);
    EXPECT_EQ(arbiter.received(), 1u);
}

TEST_F(ArbiterTest, ReceiveIsRearmedAfterReadingAFrame)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.clearOps();
    driver.deliver({0x01, 0x02});
    arbiter.pumpMeshcore();
    EXPECT_TRUE(driver.didOp("startReceive"));
}

TEST_F(ArbiterTest, QueuedFrameIsTransmittedVerbatim)
{
    const std::vector<uint8_t> frame = {0x15, 0x00, 0x11, 0x22, 0x33};

    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    ASSERT_TRUE(arbiter.queueTx(frame.data(), frame.size()));
    arbiter.pumpMeshcore();

    ASSERT_EQ(driver.sent.size(), 1u);
    EXPECT_EQ(driver.sent.front().bytes, frame);
}

TEST_F(ArbiterTest, TransmitCompletesOnIrqAndRearmsReceive)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    ASSERT_TRUE(arbiter.queueTx(reinterpret_cast<const uint8_t *>("hi"), 2));
    arbiter.pumpMeshcore();
    EXPECT_TRUE(arbiter.meshcoreTxBusy());

    driver.irq = true;
    driver.clearOps();
    arbiter.pumpMeshcore();

    EXPECT_FALSE(arbiter.meshcoreTxBusy());
    EXPECT_EQ(arbiter.sent(), 1u);
    EXPECT_TRUE(driver.didOp("finishTransmit"));
    EXPECT_TRUE(driver.didOp("startReceive"));
}

TEST_F(ArbiterTest, ReceiveTakesPriorityOverPendingTransmit)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    ASSERT_TRUE(arbiter.queueTx(reinterpret_cast<const uint8_t *>("out"), 3));
    driver.deliver({0x09, 0x09});
    arbiter.pumpMeshcore();

    EXPECT_TRUE(arbiter.rxAvailable());
    EXPECT_TRUE(driver.sent.empty());
}

TEST_F(ArbiterTest, TxQueueRejectsOverflowAndCounts)
{
    const uint8_t payload[4] = {1, 2, 3, 4};
    for (size_t i = 0; i < kArbiterTxDepth; i++)
        EXPECT_TRUE(arbiter.queueTx(payload, sizeof(payload)));

    EXPECT_FALSE(arbiter.queueTx(payload, sizeof(payload)));
    EXPECT_EQ(arbiter.txDropped(), 1u);
}

TEST_F(ArbiterTest, OversizedAndEmptyFramesAreRejected)
{
    std::vector<uint8_t> huge(kArbiterFrameSize + 1, 0xAA);
    EXPECT_FALSE(arbiter.queueTx(huge.data(), huge.size()));
    EXPECT_FALSE(arbiter.queueTx(nullptr, 4));
    EXPECT_FALSE(arbiter.queueTx(huge.data(), 0));
}

TEST_F(ArbiterTest, QuietCadRearmsReceiveRatherThanLeaving)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.clearOps();
    driver.cadDetected = false;

    EXPECT_FALSE(arbiter.channelActive());
    EXPECT_TRUE(driver.didOp("scanChannel"));
    EXPECT_TRUE(driver.didOp("startReceive"));
    EXPECT_TRUE(arbiter.leaseHeld());
}

TEST_F(ArbiterTest, CadDetectionReportsActive)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.clearOps();
    driver.cadDetected = true;

    EXPECT_TRUE(arbiter.channelActive());
    // A CAD "detected" result never leaves a packet in the modem's buffer -
    // the receiver must be re-armed immediately so the frame CAD just found
    // is actually captured, instead of the next drainRx() reading an empty
    // buffer and dropping it.
    EXPECT_TRUE(driver.didOp("startReceive"));
}

TEST_F(ArbiterTest, PendingIrqShortCircuitsCad)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.clearOps();
    driver.irq = true;

    EXPECT_TRUE(arbiter.channelActive());
    EXPECT_FALSE(driver.didOp("scanChannel"));
}

TEST_F(ArbiterTest, OversizedInboundFrameIsDiscardedNotOverflowed)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.pendingRx.assign(kArbiterFrameSize + 8, 0x5A);
    driver.irq = true;
    arbiter.pumpMeshcore();

    EXPECT_FALSE(arbiter.rxAvailable());
    EXPECT_EQ(arbiter.received(), 0u);
}

TEST_F(ArbiterTest, RssiAndSnrComeFromTheReceivedFrame)
{
    driver.rssi = -87.5f;
    driver.snr = 9.25f;

    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.deliver({0x01});
    arbiter.pumpMeshcore();

    EXPECT_FLOAT_EQ(arbiter.lastRssi(), -87.5f);
    EXPECT_FLOAT_EQ(arbiter.lastSnr(), 9.25f);
}
