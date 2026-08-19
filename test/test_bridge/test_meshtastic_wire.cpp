#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <pb_decode.h>
#include <pb_encode.h>

#include "host_mesh.h"
#include "meshcompromise/injected_text.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kBroadcast = 0xFFFFFFFFu;

meshtastic_MeshPacket freshPacket(uint32_t id = 0x1234ABCD)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.from = 0xAABBCCDD;
    packet.id = id;
    return packet;
}

std::vector<uint8_t> encodePacket(const meshtastic_MeshPacket &packet)
{
    std::vector<uint8_t> buffer(1024, 0);
    pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());
    if (!pb_encode(&stream, &meshtastic_MeshPacket_msg, &packet))
        return {};
    buffer.resize(stream.bytes_written);
    return buffer;
}

bool decodePacket(const std::vector<uint8_t> &bytes, meshtastic_MeshPacket &out)
{
    out = meshtastic_MeshPacket_init_default;
    pb_istream_t stream = pb_istream_from_buffer(bytes.data(), bytes.size());
    return pb_decode(&stream, &meshtastic_MeshPacket_msg, &out);
}

std::string payloadOf(const meshtastic_MeshPacket &packet)
{
    return std::string(reinterpret_cast<const char *>(packet.decoded.payload.bytes), packet.decoded.payload.size);
}

} // namespace

TEST(MeshtasticWire, PayloadCapacityMatchesTheConstantWeTruncateTo)
{
    meshtastic_MeshPacket packet = freshPacket();
    EXPECT_EQ(sizeof(packet.decoded.payload.bytes), kMeshtasticMaxText);
}

TEST(MeshtasticWire, InjectedTextIsATextMessageBroadcast)
{
    meshtastic_MeshPacket packet = freshPacket();
    ASSERT_TRUE(buildInjectedText(packet, kBroadcast, 0, "MC01: hi", 8));

    EXPECT_EQ(packet.which_payload_variant, meshtastic_MeshPacket_decoded_tag);
    EXPECT_EQ(packet.decoded.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    EXPECT_EQ(packet.to, kBroadcast);
    EXPECT_EQ(packet.channel, 0);
    EXPECT_EQ(payloadOf(packet), "MC01: hi");
}

TEST(MeshtasticWire, InjectedTextRoundTripsThroughMeshtasticsOwnCodec)
{
    meshtastic_MeshPacket packet = freshPacket();
    ASSERT_TRUE(buildInjectedText(packet, kBroadcast, 0, "MC01: over the air", 18));

    const std::vector<uint8_t> wire = encodePacket(packet);
    ASSERT_FALSE(wire.empty());

    meshtastic_MeshPacket decoded;
    ASSERT_TRUE(decodePacket(wire, decoded));

    EXPECT_EQ(decoded.which_payload_variant, meshtastic_MeshPacket_decoded_tag);
    EXPECT_EQ(decoded.decoded.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    EXPECT_EQ(decoded.to, kBroadcast);
    EXPECT_EQ(decoded.id, packet.id);
    EXPECT_EQ(decoded.from, packet.from);
    EXPECT_EQ(payloadOf(decoded), "MC01: over the air");
}

TEST(MeshtasticWire, AMaximumLengthPayloadStillEncodes)
{
    meshtastic_MeshPacket packet = freshPacket();
    const std::string text(kMeshtasticMaxText, 'x');
    ASSERT_TRUE(buildInjectedText(packet, kBroadcast, 0, text.c_str(), text.size()));
    EXPECT_EQ(packet.decoded.payload.size, kMeshtasticMaxText);

    meshtastic_MeshPacket decoded;
    ASSERT_TRUE(decodePacket(encodePacket(packet), decoded));
    EXPECT_EQ(payloadOf(decoded), text);
}

TEST(MeshtasticWire, OversizedTextIsTruncatedToTheProtobufCapacity)
{
    meshtastic_MeshPacket packet = freshPacket();
    const std::string text(400, 'y');
    ASSERT_TRUE(buildInjectedText(packet, kBroadcast, 0, text.c_str(), text.size()));

    EXPECT_EQ(packet.decoded.payload.size, kMeshtasticMaxText);

    meshtastic_MeshPacket decoded;
    ASSERT_TRUE(decodePacket(encodePacket(packet), decoded));
    EXPECT_EQ(decoded.decoded.payload.size, kMeshtasticMaxText);
}

TEST(MeshtasticWire, OversizedMultibyteTextIsTruncatedOnACharacterBoundary)
{
    meshtastic_MeshPacket packet = freshPacket();
    std::string text;
    while (text.size() < 400)
        text += "\xE2\x82\xAC";

    ASSERT_TRUE(buildInjectedText(packet, kBroadcast, 0, text.c_str(), text.size()));

    EXPECT_EQ(packet.decoded.payload.size % 3, 0u);
    EXPECT_LE(packet.decoded.payload.size, kMeshtasticMaxText);
    EXPECT_GT(packet.decoded.payload.size, kMeshtasticMaxText - 3);
}

TEST(MeshtasticWire, EmptyTextIsRejectedRatherThanSent)
{
    meshtastic_MeshPacket packet = freshPacket();
    EXPECT_FALSE(buildInjectedText(packet, kBroadcast, 0, "", 0));
    EXPECT_FALSE(buildInjectedText(packet, kBroadcast, 0, nullptr, 5));
    EXPECT_EQ(packet.decoded.payload.size, 0);
}

TEST(MeshtasticWire, MeshcoreTextBecomesASendableMeshtasticPacket)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode bridge(2);

    const std::string text = "hello from the other mesh";
    bridge.takeRadio();
    sender.takeRadio();
    ASSERT_TRUE(sender.stack.sendGroupText(0, "MC01", text.c_str(), text.size()));
    sender.pump();

    ASSERT_TRUE(carryOverAir(air, bridge, lastFrame(sender.driver)));
    bridge.pump();

    ASSERT_EQ(bridge.collector.texts.size(), 1u);

    meshtastic_MeshPacket packet = freshPacket();
    const std::string &received = bridge.collector.texts.front();
    ASSERT_TRUE(buildInjectedText(packet, kBroadcast, 0, received.c_str(), received.size()));

    meshtastic_MeshPacket decoded;
    ASSERT_TRUE(decodePacket(encodePacket(packet), decoded));
    EXPECT_EQ(decoded.decoded.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    EXPECT_EQ(payloadOf(decoded), "MC01: " + text);
}
