#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Packet.h>

#include "host_mesh.h"

using namespace meshcompromise;

namespace
{

void introduce(VirtualAir &air, HostNode &from, HostNode &to, const char *name)
{
    from.takeRadio();
    EXPECT_TRUE(from.stack.sendAdvert(name));
    from.pump();

    to.takeRadio();
    ASSERT_TRUE(carryOverAir(air, to, lastFrame(from.driver)));
    to.pump();
}

uint32_t nodeNumOf(const HostNode &node)
{
    return nodeNumFromPubKey(node.stack.self_id.pub_key);
}

} // namespace

TEST(DirectMessage, NodeNumIsDerivedFromThePublicKeyPrefix)
{
    uint8_t key[PUB_KEY_SIZE] = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(nodeNumFromPubKey(key), 0x12345678u);
}

TEST(DirectMessage, NodeNumAvoidsTheReservedMeshtasticValues)
{
    uint8_t zero[PUB_KEY_SIZE] = {0x00, 0x00, 0x00, 0x00};
    uint8_t broadcast[PUB_KEY_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t noLora[PUB_KEY_SIZE] = {0x00, 0x00, 0x00, 0x01};

    EXPECT_NE(nodeNumFromPubKey(zero), 0u);
    EXPECT_NE(nodeNumFromPubKey(broadcast), 0xFFFFFFFFu);
    EXPECT_NE(nodeNumFromPubKey(noLora), 1u);
}

TEST(DirectMessage, NoContactsAreKnownBeforeAnyAdvert)
{
    HostNode node(1);
    EXPECT_EQ(node.stack.contactCount(), 0);
}

TEST(DirectMessage, AnAdvertBecomesAContact)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    introduce(air, sender, listener, "PeerOne");

    ASSERT_EQ(listener.stack.contactCount(), 1);
    ASSERT_EQ(listener.collector.contacts.size(), 1u);
    EXPECT_STREQ(listener.collector.contacts.front().name, "PeerOne");
    EXPECT_EQ(listener.collector.contacts.front().nodeNum, nodeNumOf(sender));
}

TEST(DirectMessage, TheSameAdvertTwiceDoesNotDuplicateTheContact)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    introduce(air, sender, listener, "PeerOne");
    const std::vector<uint8_t> frame = lastFrame(sender.driver);

    listener.stack.setTextSink(&listener.collector);
    ASSERT_TRUE(carryOverAir(air, listener, frame));
    listener.pump();

    EXPECT_EQ(listener.stack.contactCount(), 1);
}

TEST(DirectMessage, TheContactIsFoundByItsDerivedNodeNum)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    introduce(air, sender, listener, "PeerOne");

    const MeshcoreContact *contact = listener.stack.contactByNodeNum(nodeNumOf(sender));
    ASSERT_NE(contact, nullptr);
    EXPECT_EQ(std::memcmp(contact->pubKey, sender.stack.self_id.pub_key, PUB_KEY_SIZE), 0);
    EXPECT_EQ(listener.stack.contactByNodeNum(0xDEADBEEF), nullptr);
}

TEST(DirectMessage, SendingToAnUnknownNodeFails)
{
    HostNode node(1);
    node.takeRadio();
    EXPECT_FALSE(node.stack.sendDirectText(0xDEADBEEF, "nobody home", 11));
    EXPECT_EQ(node.stack.directsSent(), 0u);
}

TEST(DirectMessage, ADirectMessageReachesTheIntendedContact)
{
    VirtualAir air;
    HostNode alice(1);
    HostNode bob(2);

    introduce(air, alice, bob, "Alice");
    introduce(air, bob, alice, "Bob");

    const std::string text = "meet me at the repeater";
    alice.takeRadio();
    ASSERT_TRUE(alice.stack.sendDirectText(nodeNumOf(bob), text.c_str(), text.size()));
    alice.pump();

    bob.takeRadio();
    ASSERT_TRUE(carryOverAir(air, bob, lastFrame(alice.driver)));
    bob.pump();

    EXPECT_EQ(bob.stack.directsHeard(), 1u);
    ASSERT_EQ(bob.collector.directs.size(), 1u);
    EXPECT_EQ(bob.collector.directs.front(), text);
    EXPECT_EQ(bob.collector.directFrom.front(), nodeNumOf(alice));
    EXPECT_TRUE(bob.collector.texts.empty());
}

TEST(DirectMessage, TheFrameDecodesAsAMeshcoreTextMessage)
{
    VirtualAir air;
    HostNode alice(1);
    HostNode bob(2);

    introduce(air, bob, alice, "Bob");

    alice.takeRadio();
    ASSERT_TRUE(alice.stack.sendDirectText(nodeNumOf(bob), "wire check", 10));
    alice.pump();

    mesh::Packet packet;
    const std::vector<uint8_t> frame = lastFrame(alice.driver);
    ASSERT_TRUE(packet.readFrom(frame.data(), static_cast<uint8_t>(frame.size())));

    EXPECT_EQ(packet.getPayloadType(), PAYLOAD_TYPE_TXT_MSG);
    EXPECT_EQ(packet.payload[0], bob.stack.self_id.pub_key[0]);
    EXPECT_EQ(packet.payload[1], alice.stack.self_id.pub_key[0]);
}

TEST(DirectMessage, AThirdPartyCannotReadTheDirectMessage)
{
    VirtualAir air;
    HostNode alice(1);
    HostNode bob(2);
    HostNode eve(3);

    introduce(air, bob, alice, "Bob");
    introduce(air, alice, eve, "Alice");

    alice.takeRadio();
    ASSERT_TRUE(alice.stack.sendDirectText(nodeNumOf(bob), "for bob only", 12));
    alice.pump();

    const std::vector<uint8_t> frame = lastFrame(alice.driver);

    eve.takeRadio();
    ASSERT_TRUE(carryOverAir(air, eve, frame));
    eve.pump();

    EXPECT_EQ(eve.stack.directsHeard(), 0u);
    EXPECT_TRUE(eve.collector.directs.empty());
}

TEST(DirectMessage, MultibyteTextSurvivesADirectMessage)
{
    VirtualAir air;
    HostNode alice(1);
    HostNode bob(2);

    introduce(air, alice, bob, "Alice");
    introduce(air, bob, alice, "Bob");

    const std::string text = "caf\xC3\xA9 \xE2\x82\xAC 5";
    alice.takeRadio();
    ASSERT_TRUE(alice.stack.sendDirectText(nodeNumOf(bob), text.c_str(), text.size()));
    alice.pump();

    bob.takeRadio();
    ASSERT_TRUE(carryOverAir(air, bob, lastFrame(alice.driver)));
    bob.pump();

    ASSERT_EQ(bob.collector.directs.size(), 1u);
    EXPECT_EQ(bob.collector.directs.front(), text);
}

TEST(DirectMessage, ADirectMessageIsMissedWhileMeshtasticOwnsTheRadio)
{
    VirtualAir air;
    HostNode alice(1);
    HostNode bob(2);

    introduce(air, bob, alice, "Bob");

    alice.takeRadio();
    ASSERT_TRUE(alice.stack.sendDirectText(nodeNumOf(bob), "missed", 6));
    alice.pump();

    bob.releaseRadio();
    EXPECT_FALSE(carryOverAir(air, bob, lastFrame(alice.driver)));
    bob.pump();

    EXPECT_EQ(bob.stack.directsHeard(), 0u);
}

TEST(DirectMessage, TheContactIsAlsoFoundByTheNameItAdvertised)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    introduce(air, sender, listener, "PeerOne");

    const MeshcoreContact *contact = listener.stack.contactByName("PeerOne");
    ASSERT_NE(contact, nullptr);
    EXPECT_EQ(contact->nodeNum, nodeNumOf(sender));
    EXPECT_EQ(contact, listener.stack.contactByNodeNum(nodeNumOf(sender)));
}

TEST(DirectMessage, LookingUpAnUnknownOrEmptyNameFindsNothing)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    introduce(air, sender, listener, "PeerOne");

    EXPECT_EQ(listener.stack.contactByName("SomebodyElse"), nullptr);
    EXPECT_EQ(listener.stack.contactByName(""), nullptr);
    EXPECT_EQ(listener.stack.contactByName(nullptr), nullptr);
}

TEST(DirectMessage, ReceivingADirectMessageAcknowledgesIt)
{
    VirtualAir air;
    HostNode alice(1);
    HostNode bob(2);

    introduce(air, alice, bob, "Alice");
    introduce(air, bob, alice, "Bob");

    alice.takeRadio();
    ASSERT_TRUE(alice.stack.sendDirectText(nodeNumOf(bob), "ack me", 6));
    alice.pump();

    bob.takeRadio();
    ASSERT_TRUE(carryOverAir(air, bob, lastFrame(alice.driver)));
    bob.pump();

    ASSERT_EQ(bob.stack.directsHeard(), 1u);
    EXPECT_EQ(bob.stack.directAcksSent(), 1u);
}

TEST(DirectMessage, AGroupTextIsNotAcknowledged)
{
    VirtualAir air;
    HostNode alice(1);
    HostNode bob(2);

    alice.takeRadio();
    ASSERT_TRUE(alice.stack.sendGroupText(0, "Alice", "hello everyone", 14));
    alice.pump();

    bob.takeRadio();
    ASSERT_TRUE(carryOverAir(air, bob, lastFrame(alice.driver)));
    bob.pump();

    ASSERT_FALSE(bob.collector.texts.empty());
    EXPECT_EQ(bob.stack.directAcksSent(), 0u);
}

TEST(DirectMessage, ContactsSurviveAnExportAndImportRoundTrip)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    introduce(air, sender, listener, "PeerOne");

    MeshcoreContact saved[kMeshcoreMaxContacts];
    const size_t exported = listener.stack.exportContacts(saved, kMeshcoreMaxContacts);
    ASSERT_EQ(exported, 1u);
    EXPECT_STREQ(saved[0].name, "PeerOne");

    HostNode rebooted(3);
    ASSERT_EQ(rebooted.stack.contactCount(), 0);
    EXPECT_TRUE(rebooted.stack.importContact(saved[0]));

    EXPECT_EQ(rebooted.stack.contactCount(), 1);
    const MeshcoreContact *restored = rebooted.stack.contactByNodeNum(nodeNumOf(sender));
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(std::memcmp(restored->pubKey, sender.stack.self_id.pub_key, PUB_KEY_SIZE), 0);
}

TEST(DirectMessage, ExportingIntoNoBufferOrNoRoomWritesNothing)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    introduce(air, sender, listener, "PeerOne");

    MeshcoreContact saved[1];
    EXPECT_EQ(listener.stack.exportContacts(nullptr, 4), 0u);
    EXPECT_EQ(listener.stack.exportContacts(saved, 0), 0u);
}

TEST(DirectMessage, ImportingTheSameContactTwiceKeepsOneEntry)
{
    VirtualAir air;
    HostNode sender(1);
    HostNode listener(2);

    introduce(air, sender, listener, "PeerOne");

    MeshcoreContact saved[kMeshcoreMaxContacts];
    ASSERT_EQ(listener.stack.exportContacts(saved, kMeshcoreMaxContacts), 1u);

    HostNode rebooted(3);
    ASSERT_TRUE(rebooted.stack.importContact(saved[0]));
    EXPECT_TRUE(rebooted.stack.importContact(saved[0]));
    EXPECT_EQ(rebooted.stack.contactCount(), 1);
}

TEST(DirectMessage, AnEmptyOrUnnumberedContactIsNotImported)
{
    HostNode node(1);

    MeshcoreContact unused;
    EXPECT_FALSE(node.stack.importContact(unused));

    MeshcoreContact numberless;
    numberless.used = true;
    numberless.nodeNum = 0;
    EXPECT_FALSE(node.stack.importContact(numberless));

    EXPECT_EQ(node.stack.contactCount(), 0);
}

TEST(DirectMessage, ImportingStopsWhenTheContactTableIsFull)
{
    HostNode node(1);

    for (int i = 0; i < kMeshcoreMaxContacts; i++) {
        MeshcoreContact contact;
        contact.used = true;
        contact.nodeNum = static_cast<uint32_t>(0x1000 + i);
        contact.pubKey[0] = static_cast<uint8_t>(i + 1);
        snprintf(contact.name, sizeof(contact.name), "peer%d", i);
        ASSERT_TRUE(node.stack.importContact(contact));
    }

    ASSERT_EQ(node.stack.contactCount(), static_cast<uint8_t>(kMeshcoreMaxContacts));

    MeshcoreContact overflow;
    overflow.used = true;
    overflow.nodeNum = 0x9999;
    overflow.pubKey[0] = 0xFF;
    EXPECT_FALSE(node.stack.importContact(overflow));
}

TEST(DirectMessage, AnImportedContactIsUsableAsADirectMessageTarget)
{
    VirtualAir air;
    HostNode alice(1);
    HostNode bob(2);

    introduce(air, bob, alice, "Bob");

    MeshcoreContact saved[kMeshcoreMaxContacts];
    ASSERT_EQ(alice.stack.exportContacts(saved, kMeshcoreMaxContacts), 1u);

    HostNode rebooted(4);
    ASSERT_TRUE(rebooted.stack.importContact(saved[0]));

    rebooted.takeRadio();
    EXPECT_TRUE(rebooted.stack.sendDirectText(nodeNumOf(bob), "after a reboot", 14));
}

TEST(DirectMessage, DistinctSeedsGiveDistinctIdentities)
{
    HostNode a(1);
    HostNode b(2);
    HostNode c(3);

    EXPECT_NE(std::memcmp(a.stack.self_id.pub_key, b.stack.self_id.pub_key, PUB_KEY_SIZE), 0);
    EXPECT_NE(std::memcmp(b.stack.self_id.pub_key, c.stack.self_id.pub_key, PUB_KEY_SIZE), 0);
    EXPECT_NE(std::memcmp(a.stack.self_id.pub_key, c.stack.self_id.pub_key, PUB_KEY_SIZE), 0);
}
