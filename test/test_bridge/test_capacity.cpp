#include <gtest/gtest.h>

#include <string>

#include "host_mesh.h"

#include "meshcompromise/mirror.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kUs = 0xAABBCCDDu;

MirrorSource localBroadcast(uint32_t id)
{
    MirrorSource source;
    source.packetId = id;
    source.fromNode = kUs;
    source.toNode = 0xFFFFFFFFu;
    source.isTextMessage = true;
    source.isBroadcast = true;
    return source;
}

class Capacity : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        hostMillis = 0;
        bridge = std::make_unique<HostNode>(1);
        bridge->takeRadio();
    }

    std::unique_ptr<HostNode> bridge;
};

} // namespace

TEST(MirrorCapacity, AnEchoIsStillSuppressedAfterTheHistoryIsFull)
{
    Mirror mirror{MirrorConfig()};
    mirror.setLocalNode(kUs);
    MirrorMessage message;

    ASSERT_EQ(MirrorDecision::Send, mirror.prepare(localBroadcast(1), "first", 5, message));

    for (uint32_t id = 2; id <= kMirrorHistorySize; id++)
        ASSERT_EQ(MirrorDecision::Send, mirror.prepare(localBroadcast(id), "filler", 6, message));

    EXPECT_EQ(MirrorDecision::AlreadyMirrored, mirror.prepare(localBroadcast(1), "first", 5, message));
}

TEST(MirrorCapacity, AnEchoIsForgottenOnceItFallsOutOfTheHistory)
{
    Mirror mirror{MirrorConfig()};
    mirror.setLocalNode(kUs);
    MirrorMessage message;

    ASSERT_EQ(MirrorDecision::Send, mirror.prepare(localBroadcast(1), "first", 5, message));

    for (uint32_t id = 2; id <= kMirrorHistorySize + 1; id++)
        ASSERT_EQ(MirrorDecision::Send, mirror.prepare(localBroadcast(id), "filler", 6, message));

    EXPECT_EQ(MirrorDecision::Send, mirror.prepare(localBroadcast(1), "first", 5, message));
}

TEST(MirrorCapacity, TheHistoryNeverGrowsBeyondItsBound)
{
    MirrorHistory history;
    for (uint32_t id = 0; id < kMirrorHistorySize * 8; id++)
        history.record(id);

    EXPECT_EQ(kMirrorHistorySize, history.size());
}

TEST_F(Capacity, TheContactTableFillsAndThenRefusesNewIdentities)
{
    for (int seed = 0; seed < kMeshcoreMaxContacts; seed++) {
        HostNode advertiser(static_cast<uint32_t>(100 + seed));
        advertiser.takeRadio();
        ASSERT_TRUE(advertiser.stack.sendAdvert("peer"));
        advertiser.pump();

        VirtualAir air;
        const std::vector<uint8_t> frame = lastFrame(advertiser.driver);
        ASSERT_FALSE(frame.empty());
        carryOverAir(air, *bridge, frame);
        bridge->pump();
    }

    EXPECT_EQ(kMeshcoreMaxContacts, bridge->stack.contactCount());

    HostNode late(9999);
    late.takeRadio();
    ASSERT_TRUE(late.stack.sendAdvert("late"));
    late.pump();

    VirtualAir air;
    const std::vector<uint8_t> frame = lastFrame(late.driver);
    ASSERT_FALSE(frame.empty());
    carryOverAir(air, *bridge, frame);
    bridge->pump();

    EXPECT_EQ(kMeshcoreMaxContacts, bridge->stack.contactCount());
}

TEST_F(Capacity, AKnownContactStillWorksWhenTheTableIsFull)
{
    HostNode friendly(50);
    friendly.takeRadio();
    ASSERT_TRUE(friendly.stack.sendAdvert("friend"));
    friendly.pump();

    VirtualAir first;
    carryOverAir(first, *bridge, lastFrame(friendly.driver));
    bridge->pump();

    ASSERT_EQ(1u, bridge->stack.contactCount());
    const MeshcoreContact *known = nullptr;
    for (const MeshcoreContact &contact : bridge->collector.contacts)
        known = &contact;
    ASSERT_NE(nullptr, known);
    const uint32_t nodeNum = known->nodeNum;

    for (int seed = 0; seed < kMeshcoreMaxContacts + 4; seed++) {
        HostNode advertiser(static_cast<uint32_t>(200 + seed));
        advertiser.takeRadio();
        advertiser.stack.sendAdvert("filler");
        advertiser.pump();

        VirtualAir air;
        const std::vector<uint8_t> frame = lastFrame(advertiser.driver);
        if (frame.empty())
            continue;
        carryOverAir(air, *bridge, frame);
        bridge->pump();
    }

    EXPECT_NE(nullptr, bridge->stack.contactByNodeNum(nodeNum));
}

TEST_F(Capacity, TheTransmitQueueRefusesWorkRatherThanOverflowing)
{
    const uint8_t frame[16] = {0};

    size_t accepted = 0;
    for (size_t i = 0; i < kArbiterTxDepth * 4; i++)
        if (bridge->radio.arbiter().queueTx(frame, sizeof(frame)))
            accepted++;

    EXPECT_LE(accepted, kArbiterTxDepth);
    EXPECT_GT(bridge->radio.arbiter().txDropped(), 0u);
}

TEST_F(Capacity, AnOversizedFrameIsRefusedOutright)
{
    std::vector<uint8_t> huge(kArbiterFrameSize + 1, 0xAB);

    EXPECT_FALSE(bridge->radio.arbiter().queueTx(huge.data(), huge.size()));
}

TEST_F(Capacity, AMaximumLengthGroupTextStillCrossesTheAir)
{
    HostNode peer(2);
    peer.takeRadio();
    VirtualAir air;

    const std::string text(kMeshcoreMaxText, 'x');
    ASSERT_TRUE(bridge->stack.sendGroupText(0, "abcd", text.c_str(), text.size()));
    bridge->pump();

    const std::vector<uint8_t> frame = lastFrame(bridge->driver);
    ASSERT_FALSE(frame.empty());
    ASSERT_LE(frame.size(), kArbiterFrameSize);

    ASSERT_TRUE(carryOverAir(air, peer, frame));
    peer.pump();

    ASSERT_EQ(1u, peer.collector.texts.size());

    const std::string &heard = peer.collector.texts[0];
    ASSERT_EQ(0u, heard.find("abcd: "));

    const std::string body = heard.substr(6);
    EXPECT_FALSE(body.empty());
    EXPECT_LE(heard.size(), kMeshcoreMaxText);
    EXPECT_EQ(0u, text.find(body));
}
