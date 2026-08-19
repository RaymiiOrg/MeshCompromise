#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <Packet.h>

#include "host_mesh.h"

using namespace meshcompromise;

namespace
{

constexpr size_t kAdvertPubKeyOffset = 0;
constexpr size_t kAdvertTimestampOffset = PUB_KEY_SIZE;
constexpr size_t kAdvertSignatureOffset = PUB_KEY_SIZE + 4;
constexpr size_t kAdvertAppDataOffset = PUB_KEY_SIZE + 4 + SIGNATURE_SIZE;

mesh::Packet parse(const std::vector<uint8_t> &frame)
{
    mesh::Packet packet;
    EXPECT_TRUE(packet.readFrom(frame.data(), static_cast<uint8_t>(frame.size())));
    return packet;
}

std::vector<uint8_t> advertFrom(HostNode &node, const char *name)
{
    node.takeRadio();
    EXPECT_TRUE(node.stack.sendAdvert(name));
    node.pump();
    return lastFrame(node.driver);
}

std::vector<uint8_t> groupTextFrom(HostNode &node, const char *sender, const std::string &text)
{
    node.takeRadio();
    EXPECT_TRUE(node.stack.sendGroupText(0, sender, text.c_str(), text.size()));
    node.pump();
    return lastFrame(node.driver);
}

bool advertSignatureVerifies(const mesh::Packet &packet)
{
    mesh::Identity id;
    std::memcpy(id.pub_key, &packet.payload[kAdvertPubKeyOffset], PUB_KEY_SIZE);

    const size_t appLength = packet.payload_len - kAdvertAppDataOffset;

    uint8_t message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE] = {0};
    size_t length = 0;
    std::memcpy(&message[length], id.pub_key, PUB_KEY_SIZE);
    length += PUB_KEY_SIZE;
    std::memcpy(&message[length], &packet.payload[kAdvertTimestampOffset], 4);
    length += 4;
    std::memcpy(&message[length], &packet.payload[kAdvertAppDataOffset], appLength);
    length += appLength;

    return id.verify(&packet.payload[kAdvertSignatureOffset], message, static_cast<int>(length));
}

} // namespace

TEST(MeshcoreRadio, MeshcoreCannotTransmitWithoutTheRadioLease)
{
    HostNode node(1);
    ASSERT_TRUE(node.stack.sendAdvert("MC-Bridge"));

    node.pump();

    EXPECT_TRUE(node.driver.sent.empty());
    EXPECT_FALSE(node.driver.didOp("startTransmit"));
}

TEST(MeshcoreRadio, MeshcoreWaitsForAnInboundPacketBeforeTransmitting)
{
    HostNode node(1);
    node.takeRadio();
    node.driver.inboundInProgress = true;

    ASSERT_TRUE(node.stack.sendAdvert("MC-Bridge"));
    EXPECT_TRUE(node.radio.isReceiving());

    node.pump(20, 100);
    EXPECT_TRUE(node.driver.sent.empty());

    node.driver.inboundInProgress = false;
    EXPECT_FALSE(node.radio.isReceiving());

    node.pump(20, 100);
    EXPECT_EQ(node.driver.sent.size(), 1u);
}

TEST(MeshcoreRadio, AdvertReachesTheAirOnceTheLeaseIsHeld)
{
    HostNode node(1);
    const std::vector<uint8_t> frame = advertFrom(node, "MC-Bridge");

    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(node.driver.sent.size(), 1u);
    EXPECT_EQ(node.radio.packetsSent(), 1u);
}

TEST(MeshcoreRadio, AdvertFrameDecodesAsAFloodedAdvert)
{
    HostNode node(1);
    const mesh::Packet packet = parse(advertFrom(node, "MC-Bridge"));

    EXPECT_EQ(packet.getPayloadType(), PAYLOAD_TYPE_ADVERT);
    EXPECT_TRUE(packet.isRouteFlood());
    EXPECT_EQ(packet.path_len, 0);
    EXPECT_GT(packet.payload_len, kAdvertAppDataOffset);
}

TEST(MeshcoreRadio, AdvertCarriesOurPublicKey)
{
    HostNode node(1);
    const mesh::Packet packet = parse(advertFrom(node, "MC-Bridge"));

    EXPECT_EQ(std::memcmp(&packet.payload[kAdvertPubKeyOffset], node.stack.self_id.pub_key, PUB_KEY_SIZE), 0);
}

TEST(MeshcoreRadio, AdvertAppDataParsesAsAChatNodeWithTheGivenName)
{
    HostNode node(1);
    const mesh::Packet packet = parse(advertFrom(node, "MC-Bridge"));

    const uint8_t appLength = static_cast<uint8_t>(packet.payload_len - kAdvertAppDataOffset);
    AdvertDataParser parser(&packet.payload[kAdvertAppDataOffset], appLength);

    ASSERT_TRUE(parser.isValid());
    EXPECT_EQ(parser.getType(), ADV_TYPE_CHAT);
    ASSERT_TRUE(parser.hasName());
    EXPECT_STREQ(parser.getName(), "MC-Bridge");
    EXPECT_FALSE(parser.hasLatLon());
}

TEST(MeshcoreRadio, AdvertFallsBackToTheProjectNameWhenUnnamed)
{
    HostNode node(1);
    const mesh::Packet packet = parse(advertFrom(node, ""));

    const uint8_t appLength = static_cast<uint8_t>(packet.payload_len - kAdvertAppDataOffset);
    AdvertDataParser parser(&packet.payload[kAdvertAppDataOffset], appLength);

    ASSERT_TRUE(parser.isValid());
    EXPECT_STREQ(parser.getName(), kMeshcoreAdvertName);
}

TEST(MeshcoreRadio, AdvertSignatureVerifiesAgainstTheAdvertisedKey)
{
    HostNode node(1);
    const mesh::Packet packet = parse(advertFrom(node, "MC-Bridge"));

    EXPECT_TRUE(advertSignatureVerifies(packet));
}

TEST(MeshcoreRadio, AdvertSignatureCheckRejectsATamperedName)
{
    HostNode node(1);
    mesh::Packet packet = parse(advertFrom(node, "MC-Bridge"));

    packet.payload[packet.payload_len - 1] ^= 0xFF;

    EXPECT_FALSE(advertSignatureVerifies(packet));
}

TEST(MeshcoreRadio, AdvertIsAcceptedByASecondMeshcoreNode)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    const std::vector<uint8_t> frame = advertFrom(sender, "MC-Bridge");
    ASSERT_FALSE(frame.empty());

    listener.takeRadio();
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();

    EXPECT_EQ(listener.stack.advertsHeard(), 1u);
    EXPECT_EQ(listener.radio.packetsReceived(), 1u);
}

TEST(MeshcoreRadio, ForgedAdvertIsRejectedByTheListener)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    std::vector<uint8_t> frame = advertFrom(sender, "MC-Bridge");
    ASSERT_FALSE(frame.empty());
    frame[frame.size() - 1] ^= 0xFF;

    listener.takeRadio();
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();

    EXPECT_EQ(listener.stack.advertsHeard(), 0u);
}

TEST(MeshcoreRadio, AdvertIsMissedWhileMeshtasticOwnsTheRadio)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    const std::vector<uint8_t> frame = advertFrom(sender, "MC-Bridge");
    ASSERT_FALSE(frame.empty());

    listener.releaseRadio();
    EXPECT_FALSE(carryOverAir(air, listener, frame));
    listener.pump();

    EXPECT_EQ(listener.stack.advertsHeard(), 0u);
    EXPECT_EQ(air.missed, 1u);
}

TEST(ReverseMirrorRadio, GroupTextCrossesTheAirAndReachesTheSink)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    const std::vector<uint8_t> frame = groupTextFrom(sender, "MC01", "hello from meshcore");
    ASSERT_FALSE(frame.empty());

    listener.takeRadio();
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();

    EXPECT_EQ(listener.stack.textsHeard(), 1u);
    ASSERT_EQ(listener.collector.texts.size(), 1u);
    EXPECT_EQ(listener.collector.texts.front(), "MC01: hello from meshcore");
}

TEST(ReverseMirrorRadio, GroupTextFrameDecodesAsAFloodedGroupText)
{
    HostNode sender(1);
    const mesh::Packet packet = parse(groupTextFrom(sender, "MC01", "wire check"));

    EXPECT_EQ(packet.getPayloadType(), PAYLOAD_TYPE_GRP_TXT);
    EXPECT_TRUE(packet.isRouteFlood());
}

TEST(ReverseMirrorRadio, GroupTextIsMissedWhileMeshtasticOwnsTheRadio)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    const std::vector<uint8_t> frame = groupTextFrom(sender, "MC01", "missed me");
    ASSERT_FALSE(frame.empty());

    listener.releaseRadio();
    EXPECT_FALSE(carryOverAir(air, listener, frame));
    listener.pump();

    EXPECT_TRUE(listener.collector.texts.empty());
    EXPECT_EQ(listener.stack.textsHeard(), 0u);
}

TEST(ReverseMirrorRadio, GroupTextOnAForeignChannelIsHeardButNotDecoded)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2, "AAECAwQFBgcICQoLDA0ODw==");

    const std::vector<uint8_t> frame = groupTextFrom(sender, "MC01", "private");
    ASSERT_FALSE(frame.empty());

    listener.takeRadio();
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();

    EXPECT_EQ(listener.radio.packetsReceived(), 1u);
    EXPECT_EQ(listener.stack.textsHeard(), 0u);
    EXPECT_TRUE(listener.collector.texts.empty());
}

TEST(ReverseMirrorRadio, MultibyteTextSurvivesTheRadioRoundTrip)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    const std::string text = "caf\xC3\xA9 \xE2\x82\xAC 10";
    const std::vector<uint8_t> frame = groupTextFrom(sender, "MC01", text);
    ASSERT_FALSE(frame.empty());

    listener.takeRadio();
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();

    ASSERT_EQ(listener.collector.texts.size(), 1u);
    EXPECT_EQ(listener.collector.texts.front(), "MC01: " + text);
}

TEST(ReverseMirrorRadio, OversizedTextArrivesTruncatedOnAUtf8Boundary)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    std::string text;
    while (text.size() < 400)
        text += "\xE2\x82\xAC";

    const std::vector<uint8_t> frame = groupTextFrom(sender, "MC01", text);
    ASSERT_FALSE(frame.empty());

    listener.takeRadio();
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();

    ASSERT_EQ(listener.collector.texts.size(), 1u);
    const std::string received = listener.collector.texts.front();
    EXPECT_EQ(received.compare(0, 6, "MC01: "), 0);
    EXPECT_EQ((received.size() - 6) % 3, 0u);
    EXPECT_LE(received.size(), kMeshcoreMaxText);
}

TEST(ReverseMirrorRadio, AGroupTextIsNeverDeliveredTwice)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    const std::vector<uint8_t> frame = groupTextFrom(sender, "MC01", "once only");
    ASSERT_FALSE(frame.empty());

    listener.takeRadio();
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();

    EXPECT_EQ(listener.radio.packetsReceived(), 2u);
    EXPECT_EQ(listener.collector.texts.size(), 1u);
}

TEST(ReverseMirrorRadio, ASenderIgnoresItsOwnGroupTextComingBack)
{
    VirtualAir air;
    HostNode sender(1);

    const std::vector<uint8_t> frame = groupTextFrom(sender, "MC01", "echo");
    ASSERT_FALSE(frame.empty());

    ASSERT_TRUE(carryOverAir(air, sender, frame));
    sender.pump();

    EXPECT_TRUE(sender.collector.texts.empty());
}
