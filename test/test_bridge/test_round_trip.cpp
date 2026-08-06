#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "host_mesh.h"
#include "meshtastic_wire.h"

#include "meshcompromise/injected_text.h"
#include "meshcompromise/mirror.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kUs = 0xAABBCCDDu;
constexpr uint32_t kPacketId = 0x5150C0DEu;

} // namespace

class RoundTrip : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        hostMillis = 0;
        bridge = std::make_unique<HostNode>(1);
        peer = std::make_unique<HostNode>(2);
        mirror.setLocalNode(kUs);
        bridge->takeRadio();
        peer->takeRadio();
    }

    std::unique_ptr<HostNode> bridge;
    std::unique_ptr<HostNode> peer;
    VirtualAir air;
    Mirror mirror{MirrorConfig()};

    bool meshtasticToMeshcore(const std::string &text, uint32_t packetId = kPacketId)
    {
        MirrorSource source;
        source.packetId = packetId;
        source.fromNode = kUs;
        source.toNode = 0xFFFFFFFFu;
        source.channel = 0;
        source.isTextMessage = true;
        source.isBroadcast = true;

        MirrorMessage message;
        if (mirror.prepare(source, text.c_str(), text.size(), message) != MirrorDecision::Send)
            return false;

        bridge->driver.sent.clear();
        if (!bridge->stack.sendGroupText(0, "abcd", reinterpret_cast<const char *>(message.payload), message.length))
            return false;
        bridge->pump();

        const std::vector<uint8_t> frame = lastFrame(bridge->driver);
        if (frame.empty())
            return false;

        if (!carryOverAir(air, *peer, frame))
            return false;
        peer->pump();
        return true;
    }

    bool meshcoreToMeshtastic(const std::string &text, std::string &recovered)
    {
        peer->driver.sent.clear();
        if (!peer->stack.sendGroupText(0, "peer", text.c_str(), text.size()))
            return false;
        peer->pump();

        const std::vector<uint8_t> frame = lastFrame(peer->driver);
        if (frame.empty())
            return false;

        if (!carryOverAir(air, *bridge, frame))
            return false;
        bridge->pump();

        if (bridge->collector.texts.empty())
            return false;

        const std::string &heard = bridge->collector.texts.back();

        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
        packet.from = kUs;
        packet.id = kPacketId + 1;
        if (!buildInjectedText(packet, 0xFFFFFFFFu, 0, heard.c_str(), heard.size()))
            return false;

        const std::vector<uint8_t> wire = encodeMeshtasticFrame(packet, primaryChannelKey(), primaryChannelHash());

        MeshtasticHeader header;
        meshtastic_Data data = meshtastic_Data_init_default;
        if (!decodeMeshtasticFrame(wire, primaryChannelKey(), header, data))
            return false;

        recovered = dataText(data);
        return true;
    }
};

TEST_F(RoundTrip, AMeshtasticBroadcastIsReadByAnIndependentMeshcoreNode)
{
    const std::string text = "hello from meshtastic";

    ASSERT_TRUE(meshtasticToMeshcore(text));

    ASSERT_EQ(1u, peer->collector.texts.size());
    EXPECT_NE(std::string::npos, peer->collector.texts[0].find(text));
}

TEST_F(RoundTrip, TheReceivingNodeSeesOurSenderName)
{

    ASSERT_TRUE(meshtasticToMeshcore("who sent this"));

    ASSERT_EQ(1u, peer->collector.texts.size());
    EXPECT_EQ(0u, peer->collector.texts[0].find("abcd: "));
}

TEST_F(RoundTrip, AMeshcoreBroadcastComesBackOutAsAMeshtasticFrame)
{
    std::string recovered;

    ASSERT_TRUE(meshcoreToMeshtastic("hello from meshcore", recovered));

    EXPECT_NE(std::string::npos, recovered.find("hello from meshcore"));
    EXPECT_NE(std::string::npos, recovered.find("peer"));
}

TEST_F(RoundTrip, TextSurvivesBothCrossingsUnchanged)
{
    const std::string outbound = "ping across the bridge";
    std::string recovered;

    ASSERT_TRUE(meshtasticToMeshcore(outbound));
    ASSERT_EQ(1u, peer->collector.texts.size());

    ASSERT_TRUE(meshcoreToMeshtastic("pong back over the bridge", recovered));
    EXPECT_NE(std::string::npos, recovered.find("pong back over the bridge"));
}

TEST_F(RoundTrip, MultibyteTextSurvivesTheWholeChain)
{
    const std::string text = "\xE2\x9C\x93 caf\xC3\xA9 \xF0\x9F\x93\xA1";

    ASSERT_TRUE(meshtasticToMeshcore(text));

    ASSERT_EQ(1u, peer->collector.texts.size());
    EXPECT_NE(std::string::npos, peer->collector.texts[0].find(text));
}

TEST_F(RoundTrip, AnEavesdropperOnAnotherChannelHearsNothing)
{
    HostNode stranger(3, "AAAAAAAAAAAAAAAAAAAAAA==");
    stranger.takeRadio();

    ASSERT_TRUE(bridge->stack.sendGroupText(0, "abcd", "secret to our channel", 21));
    bridge->pump();

    const std::vector<uint8_t> frame = lastFrame(bridge->driver);
    ASSERT_FALSE(frame.empty());

    carryOverAir(air, stranger, frame);
    stranger.pump();

    EXPECT_TRUE(stranger.collector.texts.empty());
}

TEST_F(RoundTrip, AMeshtasticNodeOnAnotherChannelCannotReadTheReturnedFrame)
{
    std::string recovered;
    ASSERT_TRUE(meshcoreToMeshtastic("only for our mesh", recovered));

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.from = kUs;
    packet.id = kPacketId;
    ASSERT_TRUE(buildInjectedText(packet, 0xFFFFFFFFu, 0, recovered.c_str(), recovered.size()));

    const std::vector<uint8_t> wire = encodeMeshtasticFrame(packet, primaryChannelKey(), primaryChannelHash());

    CryptoKey wrong = primaryChannelKey();
    wrong.bytes[0] ^= 0xFF;

    MeshtasticHeader header;
    meshtastic_Data data = meshtastic_Data_init_default;
    const bool decoded = decodeMeshtasticFrame(wire, wrong, header, data);

    EXPECT_FALSE(decoded && dataText(data) == recovered);
}

TEST_F(RoundTrip, AFrameIsMissedWhenTheBridgeHasHandedTheRadioBack)
{

    peer->driver.sent.clear();
    ASSERT_TRUE(peer->stack.sendGroupText(0, "peer", "while we were away", 18));
    peer->pump();

    const std::vector<uint8_t> frame = lastFrame(peer->driver);
    ASSERT_FALSE(frame.empty());

    bridge->releaseRadio();
    carryOverAir(air, *bridge, frame);
    bridge->pump();

    EXPECT_TRUE(bridge->collector.texts.empty());
}

TEST_F(RoundTrip, AnEchoOfOurOwnMirroredTextIsNotMirroredAgain)
{
    const std::string text = "do not loop me";

    ASSERT_TRUE(meshtasticToMeshcore(text, kPacketId));
    EXPECT_FALSE(meshtasticToMeshcore(text, kPacketId));
}
