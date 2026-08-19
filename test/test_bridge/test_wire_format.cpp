#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <Packet.h>
#include <Utils.h>

#include "meshcompromise/base64.h"
#include "meshcompromise/mirror.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kLocalNode = 0xAABBCCDD;
constexpr const char *kPublicPsk = "izOH6cXN6mrJ5e26oRXNcg==";

MirrorSource localBroadcast(uint32_t id)
{
    MirrorSource source;
    source.packetId = id;
    source.fromNode = kLocalNode;
    source.toNode = 0xFFFFFFFF;
    source.isTextMessage = true;
    source.isBroadcast = true;
    return source;
}

Mirror makeMirror()
{
    MirrorConfig config;
    Mirror mirror(config);
    mirror.setLocalNode(kLocalNode);
    return mirror;
}

struct Channel {
    uint8_t secret[32] = {0};
    uint8_t hash[1] = {0};
    size_t secretLength = 0;
};

Channel publicChannel()
{
    Channel channel;
    channel.secretLength = decodeBase64(kPublicPsk, channel.secret, sizeof(channel.secret));
    mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret, static_cast<int>(channel.secretLength));
    return channel;
}

GroupTextPayload payloadFor(const std::string &text, const char *sender = "MC01", uint32_t timestamp = 0x5F5E1000)
{
    GroupTextPayload payload;
    EXPECT_TRUE(buildGroupTextPayload(timestamp, sender, text.c_str(), text.size(), payload));
    return payload;
}

} // namespace

TEST(Base64, DecodesTheMeshcorePublicPsk)
{
    uint8_t out[32] = {0};
    ASSERT_EQ(decodeBase64(kPublicPsk, out, sizeof(out)), 16u);

    const uint8_t expected[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                 0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};
    EXPECT_EQ(std::memcmp(out, expected, sizeof(expected)), 0);
}

TEST(Base64, ChannelHashMatchesMeshcoreDerivation)
{
    const Channel channel = publicChannel();
    EXPECT_EQ(channel.secretLength, 16u);
    EXPECT_EQ(channel.hash[0], 0x11);
}

TEST(Base64, RejectsGarbageAndRespectsCapacity)
{
    uint8_t out[32] = {0};
    EXPECT_EQ(decodeBase64("not*valid", out, sizeof(out)), 0u);
    EXPECT_EQ(decodeBase64(kPublicPsk, out, 4), 0u);
    EXPECT_EQ(decodeBase64(nullptr, out, sizeof(out)), 0u);
}

TEST(GroupText, HeaderCarriesTimestampAndPlainType)
{
    const GroupTextPayload payload = payloadFor("hello", "MC01", 0x11223344);

    uint32_t timestamp = 0;
    std::memcpy(&timestamp, payload.bytes, 4);
    EXPECT_EQ(timestamp, 0x11223344u);
    EXPECT_EQ(payload.bytes[4], kGroupTextPlain);
}

TEST(GroupText, BodyIsSenderColonSpaceText)
{
    const GroupTextPayload payload = payloadFor("hello mesh", "MC01");
    const std::string body(reinterpret_cast<const char *>(payload.bytes + kGroupTextHeader),
                           payload.length - kGroupTextHeader);
    EXPECT_EQ(body, "MC01: hello mesh");
}

TEST(GroupText, LengthExcludesTheNullTerminator)
{
    const GroupTextPayload payload = payloadFor("abc", "MC01");
    EXPECT_EQ(payload.length, kGroupTextHeader + std::strlen("MC01: abc"));
    EXPECT_EQ(payload.bytes[payload.length], 0);
}

TEST(GroupText, TextIsTruncatedToLeaveRoomForThePrefix)
{
    const std::string text(400, 'x');
    const GroupTextPayload payload = payloadFor(text, "LongSenderName");

    EXPECT_TRUE(payload.truncated);
    EXPECT_LE(payload.length - kGroupTextHeader, kMeshcoreMaxText);
}

TEST(GroupText, TruncationKeepsUtf8Whole)
{
    std::string text;
    while (text.size() < 400)
        text += "\xE2\x82\xAC";

    const GroupTextPayload payload = payloadFor(text, "MC01");
    const size_t textBytes = payload.length - kGroupTextHeader - std::strlen("MC01: ");
    EXPECT_EQ(textBytes % 3, 0u);
}

TEST(GroupText, RejectsEmptyAndMissingInputs)
{
    GroupTextPayload payload;
    EXPECT_FALSE(buildGroupTextPayload(0, "MC01", "", 0, payload));
    EXPECT_FALSE(buildGroupTextPayload(0, nullptr, "hi", 2, payload));
    EXPECT_FALSE(buildGroupTextPayload(0, "MC01", nullptr, 2, payload));
    EXPECT_FALSE(buildGroupTextPayload(0, "", "hi", 2, payload));
}

TEST(GroupText, FitsInsideWhatCreateGroupDatagramAccepts)
{
    const std::string text(400, 'y');
    const GroupTextPayload payload = payloadFor(text, "MC01");
    EXPECT_LE(payload.length + 1 + CIPHER_BLOCK_SIZE - 1, static_cast<size_t>(MAX_PACKET_PAYLOAD));
}

TEST(GroupText, SurvivesMeshcoreEncryptThenMacRoundTrip)
{
    const Channel channel = publicChannel();
    const GroupTextPayload payload = payloadFor("round trip", "MC01");

    uint8_t body[MAX_PACKET_PAYLOAD] = {0};
    size_t index = 0;
    body[index++] = channel.hash[0];
    const int sealed = mesh::Utils::encryptThenMAC(channel.secret, &body[index], payload.bytes,
                                                  static_cast<int>(payload.length));
    ASSERT_GT(sealed, 0);
    index += static_cast<size_t>(sealed);

    mesh::Packet packet;
    packet.header = meshcoreHeaderByte(kMeshcoreRouteFlood, kMeshcorePayloadGrpTxt);
    packet.path_len = 0;
    packet.payload_len = static_cast<uint16_t>(index);
    std::memcpy(packet.payload, body, index);

    uint8_t wire[256] = {0};
    const uint8_t wireLength = packet.writeTo(wire);

    mesh::Packet decoded;
    ASSERT_TRUE(decoded.readFrom(wire, wireLength));
    EXPECT_EQ(decoded.getPayloadType(), PAYLOAD_TYPE_GRP_TXT);
    EXPECT_EQ(decoded.payload[0], channel.hash[0]);

    uint8_t plain[MAX_PACKET_PAYLOAD] = {0};
    const int opened = mesh::Utils::MACThenDecrypt(channel.secret, plain, &decoded.payload[1], decoded.payload_len - 1);
    ASSERT_GE(opened, static_cast<int>(payload.length));

    EXPECT_EQ(std::memcmp(plain, payload.bytes, payload.length), 0);

    const std::string body_text(reinterpret_cast<const char *>(plain + kGroupTextHeader),
                               std::strlen(reinterpret_cast<const char *>(plain + kGroupTextHeader)));
    EXPECT_EQ(body_text, "MC01: round trip");
}

TEST(GroupText, WrongChannelSecretFailsToOpen)
{
    const Channel channel = publicChannel();
    const GroupTextPayload payload = payloadFor("secret", "MC01");

    uint8_t sealedBuf[MAX_PACKET_PAYLOAD] = {0};
    const int sealed = mesh::Utils::encryptThenMAC(channel.secret, sealedBuf, payload.bytes,
                                                   static_cast<int>(payload.length));
    ASSERT_GT(sealed, 0);

    uint8_t wrong[32] = {0};
    std::memcpy(wrong, channel.secret, sizeof(wrong));
    wrong[0] ^= 0xFF;

    uint8_t plain[MAX_PACKET_PAYLOAD] = {0};
    EXPECT_EQ(mesh::Utils::MACThenDecrypt(wrong, plain, sealedBuf, sealed), 0);
}

TEST(GroupText, MirroredMessageBecomesAValidGroupText)
{
    Mirror mirror = makeMirror();
    MirrorMessage message;
    const std::string text = "from meshtastic";

    ASSERT_EQ(mirror.prepare(localBroadcast(1), text.c_str(), text.size(), message), MirrorDecision::Send);

    GroupTextPayload payload;
    ASSERT_TRUE(buildGroupTextPayload(1234, "MC01", reinterpret_cast<const char *>(message.payload), message.length,
                                      payload));

    const std::string body(reinterpret_cast<const char *>(payload.bytes + kGroupTextHeader),
                           payload.length - kGroupTextHeader);
    EXPECT_EQ(body, "MC01: from meshtastic");
}

TEST(WireFormat, HeaderByteMatchesMeshcoreBitLayout)
{
    mesh::Packet packet;
    packet.header = meshcoreHeaderByte(kMeshcoreRouteFlood, kMeshcorePayloadGrpTxt);

    EXPECT_EQ(packet.getRouteType(), ROUTE_TYPE_FLOOD);
    EXPECT_EQ(packet.getPayloadType(), PAYLOAD_TYPE_GRP_TXT);
    EXPECT_EQ(packet.getPayloadVer(), PAYLOAD_VER_1);
    EXPECT_TRUE(packet.isRouteFlood());
}
