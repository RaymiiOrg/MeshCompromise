#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "meshcompromise/bridge_log.h"
#include "meshcompromise/radio_arbiter.h"

#include "fake_sx_driver.h"

using namespace meshcompromise;

namespace
{

struct Captured {
    BridgeLogLevel level;
    std::string message;
};

std::vector<Captured> *captured = nullptr;

void capture(BridgeLogLevel level, const char *message)
{
    if (captured != nullptr)
        captured->push_back({level, message});
}

class BridgeLogTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        lines.clear();
        captured = &lines;
        bridgeLog = &capture;
    }

    void TearDown() override
    {
        bridgeLog = nullptr;
        captured = nullptr;
    }

    bool sawContaining(const std::string &needle) const
    {
        for (const auto &line : lines)
            if (line.message.find(needle) != std::string::npos)
                return true;
        return false;
    }

    std::vector<Captured> lines;
};

} // namespace

TEST_F(BridgeLogTest, NothingIsEmittedWithoutASink)
{
    bridgeLog = nullptr;
    MC_LOG_INFO("dropped on the floor");
    EXPECT_TRUE(lines.empty());
}

TEST_F(BridgeLogTest, TheLevelIsCarriedThrough)
{
    MC_LOG_DEBUG("d");
    MC_LOG_INFO("i");
    MC_LOG_WARN("w");
    MC_LOG_ERROR("e");

    ASSERT_EQ(4u, lines.size());
    EXPECT_EQ(BridgeLogLevel::Debug, lines[0].level);
    EXPECT_EQ(BridgeLogLevel::Info, lines[1].level);
    EXPECT_EQ(BridgeLogLevel::Warn, lines[2].level);
    EXPECT_EQ(BridgeLogLevel::Error, lines[3].level);
}

TEST_F(BridgeLogTest, ArgumentsAreFormatted)
{
    MC_LOG_INFO("sync=0x%02x len=%u", 0x12u, 47u);

    ASSERT_EQ(1u, lines.size());
    EXPECT_EQ("sync=0x12 len=47", lines[0].message);
}

TEST_F(BridgeLogTest, AnOverlongMessageIsTruncatedNotOverflowed)
{
    const std::string huge(4096, 'x');
    MC_LOG_INFO("%s", huge.c_str());

    ASSERT_EQ(1u, lines.size());
    EXPECT_LT(lines[0].message.size(), huge.size());
}

TEST_F(BridgeLogTest, ANullFormatIsIgnored)
{
    bridgeLogf(BridgeLogLevel::Info, nullptr);
    EXPECT_TRUE(lines.empty());
}

TEST_F(BridgeLogTest, OrdinaryTakingAndReleasingTheRadioStaysQuiet)
{
    // This runs tens of times a second in the field (every scan/dwell slice),
    // so it must not log anything on its own - only genuine events (a
    // received frame, a transmit, a full tx queue, an error) should. See the
    // other tests below for the events that ARE expected to log.
    FakeSxDriver driver;
    FakeHostRadio host;
    host.profile = meshtasticNarrowSlowProfile();
    host.bind(driver);
    driver.active = host.profile;

    RadioArbiter arbiter(driver, host);
    const LoraProfile profile = meshcoreDefaultProfile();

    arbiter.enterMeshcore(SwitchMode::Aligned, profile);
    arbiter.leaveMeshcore(SwitchMode::Aligned, profile);

    EXPECT_TRUE(lines.empty());
}

TEST_F(BridgeLogTest, AFullTransmitQueueIsReported)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    RadioArbiter arbiter(driver, host);

    const uint8_t frame[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (size_t i = 0; i < kArbiterTxDepth + 2; i++)
        arbiter.queueTx(frame, sizeof(frame));

    EXPECT_TRUE(sawContaining("tx queue full"));
}

TEST_F(BridgeLogTest, AReceivedMeshcoreFrameIsReported)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    host.profile = meshtasticNarrowSlowProfile();
    host.bind(driver);
    driver.active = host.profile;

    RadioArbiter arbiter(driver, host);
    const LoraProfile profile = meshcoreDefaultProfile();
    arbiter.enterMeshcore(SwitchMode::Aligned, profile);

    driver.deliver({9, 8, 7, 6, 5});
    arbiter.pumpMeshcore();

    EXPECT_TRUE(sawContaining("MeshCore rx 5 bytes"));
}
