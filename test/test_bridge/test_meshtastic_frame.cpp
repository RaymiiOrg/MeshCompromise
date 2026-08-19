#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <AES.h>

#include "host_mesh.h"
#include "meshcompromise/injected_text.h"
#include "meshtastic_wire.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
constexpr uint32_t kFromNode = 0xAABBCCDDu;

meshtastic_MeshPacket textPacket(const std::string &text, uint32_t id = 0x1234ABCD)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.from = kFromNode;
    packet.id = id;
    packet.hop_limit = 3;
    packet.hop_start = 3;
    EXPECT_TRUE(buildInjectedText(packet, kBroadcast, 0, text.c_str(), text.size()));
    return packet;
}

std::vector<uint8_t> frameFor(const meshtastic_MeshPacket &packet)
{
    return encodeMeshtasticFrame(packet, primaryChannelKey(), primaryChannelHash());
}

LoraProfile meshtasticProfile()
{
    return meshtasticNarrowSlowProfile();
}

} // namespace

TEST(MeshtasticFrame, PrimaryChannelHashMatchesUpstreamsRecipe)
{
    const uint8_t nameHash = xorHash(reinterpret_cast<const uint8_t *>(kMeshtasticPrimaryChannelName),
                                     std::strlen(kMeshtasticPrimaryChannelName));
    const uint8_t keyHash = xorHash(kMeshtasticDefaultPsk, sizeof(kMeshtasticDefaultPsk));

    EXPECT_EQ(0x02, keyHash);
    EXPECT_EQ(nameHash ^ keyHash, primaryChannelHash());
}

TEST(MeshtasticFrame, AnUnnamedChannelIsHashedUnderItsPresetName)
{
    EXPECT_STREQ("LongFast", kMeshtasticPrimaryChannelName);
    EXPECT_NE(xorHash(kMeshtasticDefaultPsk, sizeof(kMeshtasticDefaultPsk)), primaryChannelHash());
}

TEST(MeshtasticFrame, HeaderIsSixteenBytesInUpstreamsFieldOrder)
{
    MeshtasticHeader header;
    header.to = 0x11223344;
    header.from = 0x55667788;
    header.id = 0x99AABBCC;
    header.flags = 0x6B;
    header.channel = 0x02;
    header.nextHop = 0x7A;
    header.relayNode = 0x5C;

    uint8_t bytes[kMeshtasticHeaderLength] = {0};
    writeHeader(bytes, header);

    EXPECT_EQ(bytes[0], 0x44);
    EXPECT_EQ(bytes[3], 0x11);
    EXPECT_EQ(bytes[4], 0x88);
    EXPECT_EQ(bytes[8], 0xCC);
    EXPECT_EQ(bytes[12], 0x6B);
    EXPECT_EQ(bytes[13], 0x02);
    EXPECT_EQ(bytes[14], 0x7A);
    EXPECT_EQ(bytes[15], 0x5C);

    const MeshtasticHeader back = readHeader(bytes);
    EXPECT_EQ(back.to, header.to);
    EXPECT_EQ(back.from, header.from);
    EXPECT_EQ(back.id, header.id);
    EXPECT_EQ(back.flags, header.flags);
    EXPECT_EQ(back.channel, header.channel);
    EXPECT_EQ(back.nextHop, header.nextHop);
    EXPECT_EQ(back.relayNode, header.relayNode);
}

TEST(MeshtasticFrame, FlagsCarryHopLimitHopStartAndWantAck)
{
    meshtastic_MeshPacket packet = textPacket("flags");
    packet.hop_limit = 5;
    packet.hop_start = 7;
    packet.want_ack = true;

    const std::vector<uint8_t> frame = frameFor(packet);
    ASSERT_FALSE(frame.empty());

    const MeshtasticHeader header = readHeader(frame.data());
    EXPECT_EQ(header.flags & kFlagsHopLimitMask, 5);
    EXPECT_EQ((header.flags & kFlagsHopStartMask) >> kFlagsHopStartShift, 7);
    EXPECT_NE(header.flags & kFlagsWantAckMask, 0);
}

TEST(MeshtasticFrame, TheChannelByteCarriesTheHashNotTheIndex)
{
    const std::vector<uint8_t> frame = frameFor(textPacket("hash"));
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(readHeader(frame.data()).channel, primaryChannelHash());
}

TEST(MeshtasticFrame, PayloadIsActuallyEncrypted)
{
    const std::string text = "plaintext must not appear";
    const std::vector<uint8_t> frame = frameFor(textPacket(text));
    ASSERT_FALSE(frame.empty());

    const std::string wire(reinterpret_cast<const char *>(frame.data()), frame.size());
    EXPECT_EQ(wire.find(text), std::string::npos);
}

TEST(MeshtasticFrame, RoundTripsThroughUpstreamsCrypto)
{
    const std::string text = "MC01: hello from meshcore";
    const meshtastic_MeshPacket packet = textPacket(text);
    const std::vector<uint8_t> frame = frameFor(packet);
    ASSERT_FALSE(frame.empty());

    MeshtasticHeader header;
    meshtastic_Data data;
    ASSERT_TRUE(decodeMeshtasticFrame(frame, primaryChannelKey(), header, data));

    EXPECT_EQ(header.from, kFromNode);
    EXPECT_EQ(header.to, kBroadcast);
    EXPECT_EQ(header.id, packet.id);
    EXPECT_EQ(data.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    EXPECT_EQ(dataText(data), text);
}

TEST(MeshtasticFrame, TheWrongChannelKeyDoesNotRecoverTheText)
{
    const std::string text = "secret channel";
    const std::vector<uint8_t> frame = frameFor(textPacket(text));
    ASSERT_FALSE(frame.empty());

    CryptoKey wrong = primaryChannelKey();
    wrong.bytes[0] ^= 0xFF;

    MeshtasticHeader header;
    meshtastic_Data data = meshtastic_Data_init_default;
    if (decodeMeshtasticFrame(frame, wrong, header, data)) {
        EXPECT_NE(dataText(data), text);
    }
}

TEST(MeshtasticCrypto, UpstreamsCtrKeystreamMatchesAnIndependentConstruction)
{
    const uint32_t from = kFromNode;
    const uint64_t id = 0x1122334455667788ull;

    uint8_t upstream[32] = {0};
    crypto->setKey(primaryChannelKey());
    crypto->encryptPacket(from, id, sizeof(upstream), upstream);

    uint8_t nonce[16] = {0};
    std::memcpy(nonce, &id, sizeof(id));
    std::memcpy(nonce + sizeof(id), &from, sizeof(from));

    AES128 aes;
    aes.setKey(kMeshtasticDefaultPsk, sizeof(kMeshtasticDefaultPsk));

    for (uint32_t block = 0; block < sizeof(upstream) / 16; block++) {
        uint8_t counter[16] = {0};
        std::memcpy(counter, nonce, sizeof(counter));
        counter[12] = static_cast<uint8_t>((block >> 24) & 0xFF);
        counter[13] = static_cast<uint8_t>((block >> 16) & 0xFF);
        counter[14] = static_cast<uint8_t>((block >> 8) & 0xFF);
        counter[15] = static_cast<uint8_t>(block & 0xFF);

        uint8_t keystream[16] = {0};
        aes.encryptBlock(keystream, counter);

        EXPECT_EQ(std::memcmp(keystream, upstream + block * 16, 16), 0) << "block " << block;
    }
}

TEST(MeshtasticCrypto, AChangedNonceFieldChangesTheKeystream)
{
    uint8_t withId[16] = {0};
    uint8_t withOtherId[16] = {0};
    uint8_t withOtherFrom[16] = {0};

    crypto->setKey(primaryChannelKey());
    crypto->encryptPacket(kFromNode, 0x1122334455667788ull, sizeof(withId), withId);
    crypto->setKey(primaryChannelKey());
    crypto->encryptPacket(kFromNode, 0x1122334455667789ull, sizeof(withOtherId), withOtherId);
    crypto->setKey(primaryChannelKey());
    crypto->encryptPacket(kFromNode + 1, 0x1122334455667788ull, sizeof(withOtherFrom), withOtherFrom);

    EXPECT_NE(std::memcmp(withId, withOtherId, sizeof(withId)), 0);
    EXPECT_NE(std::memcmp(withId, withOtherFrom, sizeof(withId)), 0);
}

TEST(MeshtasticCrypto, EncryptionIsItsOwnInverse)
{
    const std::string text = "counter mode round trip";
    uint8_t buffer[64] = {0};
    std::memcpy(buffer, text.data(), text.size());

    crypto->setKey(primaryChannelKey());
    crypto->encryptPacket(kFromNode, 7, text.size(), buffer);
    EXPECT_NE(std::memcmp(buffer, text.data(), text.size()), 0);

    crypto->setKey(primaryChannelKey());
    crypto->decrypt(kFromNode, 7, text.size(), buffer);
    EXPECT_EQ(std::memcmp(buffer, text.data(), text.size()), 0);
}

TEST(MeshtasticFrame, TheNonceBindsTheCiphertextToThePacketId)
{
    const std::string text = "same text, different id";
    const std::vector<uint8_t> first = frameFor(textPacket(text, 0x11111111));
    const std::vector<uint8_t> second = frameFor(textPacket(text, 0x22222222));

    ASSERT_EQ(first.size(), second.size());
    EXPECT_NE(std::memcmp(first.data() + kMeshtasticHeaderLength, second.data() + kMeshtasticHeaderLength,
                          first.size() - kMeshtasticHeaderLength),
              0);
}

TEST(MeshtasticFrame, TheNonceBindsTheCiphertextToTheSender)
{
    const std::string text = "same text, different sender";
    meshtastic_MeshPacket a = textPacket(text);
    meshtastic_MeshPacket b = textPacket(text);
    b.from = 0x01020304;

    const std::vector<uint8_t> first = frameFor(a);
    const std::vector<uint8_t> second = frameFor(b);

    ASSERT_EQ(first.size(), second.size());
    EXPECT_NE(std::memcmp(first.data() + kMeshtasticHeaderLength, second.data() + kMeshtasticHeaderLength,
                          first.size() - kMeshtasticHeaderLength),
              0);
}

TEST(MeshtasticFrame, AMaximumPayloadStillFitsTheLoraLimit)
{
    const std::string text(kMeshtasticMaxText, 'x');
    const std::vector<uint8_t> frame = frameFor(textPacket(text));

    ASSERT_FALSE(frame.empty());
    EXPECT_LE(frame.size(), kMeshtasticMaxLoraPayload);
    EXPECT_GT(frame.size(), kMeshtasticHeaderLength + kMeshtasticMaxText);

    MeshtasticHeader header;
    meshtastic_Data data;
    ASSERT_TRUE(decodeMeshtasticFrame(frame, primaryChannelKey(), header, data));
    EXPECT_EQ(dataText(data), text);
}

TEST(MeshtasticFrame, AMirroredMeshcoreTextBecomesAValidMeshtasticFrame)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode bridge(2);

    const std::string text = "across both meshes";
    bridge.takeRadio();
    sender.takeRadio();
    ASSERT_TRUE(sender.stack.sendGroupText(0, "MC01", text.c_str(), text.size()));
    sender.pump();

    ASSERT_TRUE(carryOverAir(air, bridge, lastFrame(sender.driver)));
    bridge.pump();
    ASSERT_EQ(bridge.collector.texts.size(), 1u);

    const std::vector<uint8_t> frame = frameFor(textPacket(bridge.collector.texts.front()));
    ASSERT_FALSE(frame.empty());

    MeshtasticHeader header;
    meshtastic_Data data;
    ASSERT_TRUE(decodeMeshtasticFrame(frame, primaryChannelKey(), header, data));
    EXPECT_EQ(data.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    EXPECT_EQ(dataText(data), "MC01: " + text);
}

TEST(MeshtasticFrame, AMeshtasticFrameIsHeardByAMeshtasticTunedReceiver)
{
    VirtualAir air;
    FakeSxDriver driver;
    driver.active = meshtasticProfile();

    const std::vector<uint8_t> frame = frameFor(textPacket("heard"));
    ASSERT_FALSE(frame.empty());

    EXPECT_TRUE(air.deliverTo(driver, meshtasticProfile(), frame));
    EXPECT_EQ(driver.pendingRx, frame);
}

TEST(MeshtasticFrame, AMeshtasticFrameIsMissedWhileTheRadioIsOnMeshcore)
{
    VirtualAir air;
    HostNode bridge(1);
    bridge.takeRadio();

    const std::vector<uint8_t> frame = frameFor(textPacket("missed"));
    ASSERT_FALSE(frame.empty());

    EXPECT_FALSE(air.deliverTo(bridge.driver, meshtasticProfile(), frame));
    EXPECT_EQ(air.missed, 1u);
}

TEST(MeshtasticFrame, TheTwoProtocolsFramesNeverReachTheWrongStack)
{
    VirtualAir air;
    HostNode meshcoreNode(1);
    meshcoreNode.takeRadio();

    FakeSxDriver meshtasticRadio;
    meshtasticRadio.active = meshtasticProfile();

    const std::vector<uint8_t> meshtasticFrame = frameFor(textPacket("mt"));
    meshcoreNode.stack.sendGroupText(0, "MC01", "mc", 2);
    meshcoreNode.pump();
    const std::vector<uint8_t> meshcoreFrame = lastFrame(meshcoreNode.driver);

    ASSERT_FALSE(meshtasticFrame.empty());
    ASSERT_FALSE(meshcoreFrame.empty());

    EXPECT_FALSE(air.deliverTo(meshcoreNode.driver, meshtasticProfile(), meshtasticFrame));
    EXPECT_FALSE(air.deliverTo(meshtasticRadio, meshcoreDefaultProfile(), meshcoreFrame));

    EXPECT_TRUE(air.deliverTo(meshtasticRadio, meshtasticProfile(), meshtasticFrame));
    EXPECT_TRUE(air.deliverTo(meshcoreNode.driver, meshcoreDefaultProfile(), meshcoreFrame));
}
