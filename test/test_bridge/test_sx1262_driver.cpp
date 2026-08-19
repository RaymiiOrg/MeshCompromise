#include <gtest/gtest.h>

#include "meshcompromise/lora_profile.h"
#include "meshcompromise/radio_arbiter.h"
#include "meshcompromise/sx1262_driver.h"

#include "fake_radiolib_hal.h"
#include "fake_sx_driver.h"

using namespace meshcompromise;

namespace
{

class RealDriverTest : public ::testing::Test
{
  protected:
    RealDriverTest() : module_(&hal_, 0, 1, 2, 3), radio_(&module_), driver_(radio_) {}

    void SetUp() override
    {
        ASSERT_TRUE(driver_.begin());
        ASSERT_TRUE(driver_.configure(meshtasticNarrowSlowProfile(), 20));
        hal_.clear();
    }

    uint8_t syncWordMsb() { return hal_.registerValue(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB); }
    uint8_t syncWordLsb() { return hal_.registerValue(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB + 1); }

    FakeSx126xHal hal_;
    Module module_;
    SX1262 radio_;
    Sx1262Driver driver_;
};

} // namespace

TEST_F(RealDriverTest, ConfiguringTalksToARealSx1262)
{
    EXPECT_TRUE(driver_.configure(meshcoreDefaultProfile(), 17));
    EXPECT_EQ(RADIOLIB_ERR_NONE, driver_.lastError());
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_MODULATION_PARAMS));
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY));
}

TEST_F(RealDriverTest, TheSyncWordLandsInTheRegisterTheDatasheetNames)
{
    ASSERT_TRUE(driver_.setSyncWord(0x12));

    EXPECT_EQ(0x14, syncWordMsb());
    EXPECT_EQ(0x24, syncWordLsb());
}

TEST_F(RealDriverTest, MeshtasticAndMeshcoreSyncWordsDiffer)
{
    ASSERT_TRUE(driver_.setSyncWord(0x2b));
    const uint8_t meshtasticMsb = syncWordMsb();
    const uint8_t meshtasticLsb = syncWordLsb();

    ASSERT_TRUE(driver_.setSyncWord(0x12));

    EXPECT_NE(meshtasticMsb, syncWordMsb());
    EXPECT_NE(meshtasticLsb, syncWordLsb());
}

TEST_F(RealDriverTest, ASyncWordChangeIsOneRegisterWriteAndNothingElse)
{
    ASSERT_TRUE(driver_.setSyncWord(0x12));

    EXPECT_EQ(1u, hal_.countRegisterWrites(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB));
    EXPECT_FALSE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY));
    EXPECT_FALSE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_MODULATION_PARAMS));
    EXPECT_FALSE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_CALIBRATE));
    EXPECT_FALSE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_CALIBRATE_IMAGE));
}

TEST_F(RealDriverTest, AFullReconfigureRetunesThePll)
{
    ASSERT_TRUE(driver_.configure(meshcoreDefaultProfile(), 17));

    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY));
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_MODULATION_PARAMS));
}

TEST_F(RealDriverTest, AlignedSwitchingIsCheaperThanASplitReconfigure)
{
    driver_.setSyncWord(0x12);
    const size_t alignedCalls = hal_.calls.size();

    hal_.clear();
    driver_.configure(meshcoreDefaultProfile(), 17);
    const size_t splitCalls = hal_.calls.size();

    EXPECT_LT(alignedCalls, splitCalls);
}

TEST_F(RealDriverTest, StandbyAndReceiveReachTheChip)
{
    ASSERT_TRUE(driver_.standby());
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_STANDBY));

    hal_.clear();
    ASSERT_TRUE(driver_.startReceive());
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_RX));
}

TEST_F(RealDriverTest, APreambleChangeRewritesThePacketParameters)
{
    ASSERT_TRUE(driver_.setPreambleLength(32));

    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_PACKET_PARAMS));
}

TEST_F(RealDriverTest, AnIdleChipReportsNoPacketInProgress)
{
    hal_.irqFlags = 0;
    EXPECT_FALSE(driver_.packetInProgress());
}

TEST_F(RealDriverTest, ADetectedPreambleReportsAPacketInProgress)
{
    hal_.irqFlags = RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED;
    EXPECT_TRUE(driver_.packetInProgress());

    hal_.irqFlags = RADIOLIB_SX126X_IRQ_HEADER_VALID;
    EXPECT_TRUE(driver_.packetInProgress());
}

TEST_F(RealDriverTest, TransmitPushesTheFrameToTheChip)
{
    const uint8_t frame[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    EXPECT_TRUE(driver_.startTransmit(frame, sizeof(frame)));
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_WRITE_BUFFER));
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_TX));
}

TEST_F(RealDriverTest, TheIrqFlagIsOnlyClearedExplicitly)
{
    EXPECT_FALSE(driver_.irqFired());

    driver_.raiseIrq();
    EXPECT_TRUE(driver_.irqFired());
    EXPECT_TRUE(driver_.irqFired());

    driver_.clearIrq();
    EXPECT_FALSE(driver_.irqFired());
}

TEST_F(RealDriverTest, TheArbiterDrivesARealSx1262ThroughAnAlignedSlice)
{
    FakeHostRadio host;
    host.profile = meshtasticNarrowSlowProfile();
    RadioArbiter arbiter(driver_, host);

    LoraProfile meshcore = meshcoreDefaultProfile();
    meshcore.frequencyMhz = host.profile.frequencyMhz;
    meshcore.bandwidthKhz = host.profile.bandwidthKhz;
    meshcore.spreadingFactor = host.profile.spreadingFactor;

    hal_.clear();
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);

    EXPECT_EQ(1u, hal_.countRegisterWrites(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB));
    EXPECT_FALSE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY));
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_RX));
    EXPECT_EQ(0x14, syncWordMsb());
}

TEST_F(RealDriverTest, TheArbiterReprogramsTheModemForASplitSlice)
{
    FakeHostRadio host;
    RadioArbiter arbiter(driver_, host);

    hal_.clear();
    arbiter.enterMeshcore(SwitchMode::Split, meshcoreDefaultProfile());

    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY));
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_MODULATION_PARAMS));
}

TEST_F(RealDriverTest, ASplitReconfigureRestoresTheRxSensitivityPatch)
{
    hal_.registers[kSx1262RxSensitivityReg] = 0x00;

    ASSERT_TRUE(driver_.configure(meshcoreDefaultProfile(), 17));

    EXPECT_EQ(0x01, hal_.registerValue(kSx1262RxSensitivityReg) & 0x01);
}

TEST_F(RealDriverTest, HandingTheRadioBackRestoresTheRxSensitivityPatch)
{
    FakeHostRadio host;
    RadioArbiter arbiter(driver_, host);

    arbiter.enterMeshcore(SwitchMode::Split, meshcoreDefaultProfile());
    hal_.registers[kSx1262RxSensitivityReg] = 0x00;
    arbiter.leaveMeshcore(SwitchMode::Split, meshtasticNarrowSlowProfile());

    EXPECT_EQ(0x01, hal_.registerValue(kSx1262RxSensitivityReg) & 0x01);
}

TEST_F(RealDriverTest, RetuningIsCheaperThanAFullConfigure)
{
    ASSERT_TRUE(driver_.retune(meshcoreDefaultProfile(), 17));
    const size_t retuneCalls = hal_.calls.size();

    hal_.clear();
    ASSERT_TRUE(driver_.configure(meshcoreDefaultProfile(), 17));
    const size_t configureCalls = hal_.calls.size();

    EXPECT_LT(retuneCalls, configureCalls);
}

TEST_F(RealDriverTest, RetuningStillMovesEveryModemParameter)
{
    LoraProfile target = meshcoreDefaultProfile();
    target.syncWord = 0x34;
    target.preambleSymbols = 24;

    ASSERT_TRUE(driver_.retune(target, 14));

    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY));
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_MODULATION_PARAMS));
    EXPECT_TRUE(hal_.sawOpcode(RADIOLIB_SX126X_CMD_SET_PACKET_PARAMS));
    EXPECT_EQ(1u, hal_.countRegisterWrites(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB));
}

TEST_F(RealDriverTest, RetuningRestoresTheRxSensitivityPatch)
{
    hal_.registers[kSx1262RxSensitivityReg] = 0x00;

    ASSERT_TRUE(driver_.retune(meshcoreDefaultProfile(), 17));

    EXPECT_EQ(0x01, hal_.registerValue(kSx1262RxSensitivityReg) & 0x01);
}

TEST_F(RealDriverTest, AnInvalidProfileMakesRetuneFallBackAndFail)
{
    LoraProfile broken = meshcoreDefaultProfile();
    broken.spreadingFactor = 13;

    EXPECT_FALSE(driver_.retune(broken, 17));
    EXPECT_NE(RADIOLIB_ERR_NONE, driver_.lastError());
}

TEST(UnprimedDriver, RetuningBeforeAnyConfigureFallsBackToAFullConfigure)
{
    FakeSx126xHal hal;
    Module module(&hal, 0, 1, 2, 3);
    SX1262 radio(&module);
    Sx1262Driver driver(radio);

    ASSERT_TRUE(driver.begin());
    ASSERT_FALSE(driver.primed());

    EXPECT_TRUE(driver.retune(meshcoreDefaultProfile(), 17));
    EXPECT_TRUE(driver.primed());
    EXPECT_TRUE(hal.sawOpcode(RADIOLIB_SX126X_CMD_SET_MODULATION_PARAMS));
}

TEST_F(RealDriverTest, TheRfSwitchAndGainOptionsReachTheChip)
{
    Sx1262Options options;
    options.dio2AsRfSwitch = true;
    options.rxBoostedGain = true;
    driver_.setOptions(options);

    ASSERT_TRUE(driver_.configure(meshcoreDefaultProfile(), 17));

    EXPECT_TRUE(driver_.options().dio2AsRfSwitch);
    EXPECT_TRUE(driver_.options().rxBoostedGain);
}
