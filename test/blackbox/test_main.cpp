#include "FSCommon.h"
#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "mesh/MeshModule.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/Router.h"
#include "support/MockMeshService.h"
#include <pb_decode.h>
#include <pb_encode.h>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "meshcompromise/contact_id.h"
#include "meshcompromise/injected_text.h"
#include "meshcompromise/mirror.h"
#include "meshcompromise/mirror_module.h"
#include "meshcompromise/settings_store.h"
#include "meshcompromise/ui_input.h"
#include "meshtastic_wire.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kUs = 0xAABBCCDDu;
constexpr uint32_t kPacketId = 0x1234ABCDu;
constexpr uint32_t kStranger = 0x0BADF00Du;

struct SentGroupText {
    uint8_t channel;
    std::string sender;
    std::string text;
};

struct SentDirectText {
    uint32_t nodeNum;
    std::string text;
};

class FakeMeshcoreSink : public MeshcoreSink
{
  public:
    bool sendGroupText(uint8_t channelIndex, const char *senderName, const char *text, size_t length) override
    {
        groupTexts.push_back({channelIndex, senderName ? senderName : "", std::string(text, length)});
        return groupTextResult;
    }

    bool sendDirectText(uint32_t nodeNum, const char *text, size_t length) override
    {
        directTexts.push_back({nodeNum, std::string(text, length)});
        return directTextResult;
    }

    const MeshcoreContact *contactByNodeNum(uint32_t nodeNum) const override
    {
        for (const auto &contact : contacts)
            if (contact.nodeNum == nodeNum)
                return &contact;
        return nullptr;
    }

    const MeshcoreContact *contactByName(const char *name) const override
    {
        if (name == nullptr)
            return nullptr;
        for (const auto &contact : contacts)
            if (strncmp(contact.name, name, kMeshcoreNameLength) == 0)
                return &contact;
        return nullptr;
    }

    uint8_t channelCount() const override { return 1; }

    std::vector<MeshcoreContact> contacts;
    std::vector<SentGroupText> groupTexts;
    std::vector<SentDirectText> directTexts;
    bool groupTextResult = true;
    bool directTextResult = true;
};

class RecordingRadioInterface : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        transmitted.push_back(*p);
        packetPool.release(p);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override
    {
        (void)totalPacketLen;
        (void)received;
        return 0;
    }

    std::vector<meshtastic_MeshPacket> transmitted;
};

class RecordingRouter : public Router
{
  public:
    ~RecordingRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }

    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sent.push_back(*p);
        pending.push_back(p);
        return ERRNO_OK;
    }

    void releasePending()
    {
        for (meshtastic_MeshPacket *p : pending)
            packetPool.release(p);
        pending.clear();
    }

    std::vector<meshtastic_MeshPacket> sent;
    std::vector<meshtastic_MeshPacket *> pending;
};

class ProbeModule : public MeshModule
{
  public:
    ProbeModule(const char *name, bool promiscuous, bool loopback) : MeshModule(name)
    {
        isPromiscuous = promiscuous;
        loopbackOk = loopback;
    }

    uint32_t seen = 0;

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        return p != nullptr && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
               p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP;
    }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        (void)mp;
        seen++;
        return ProcessMessage::CONTINUE;
    }
};

MockMeshService *mockService = nullptr;
RecordingRouter *mockRouter = nullptr;
NodeDB *mockNodeDB = nullptr;
FakeMeshcoreSink *sink = nullptr;

meshtastic_MeshPacket textPacket(uint32_t from, uint32_t to, const std::string &text, uint32_t id = kPacketId)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.from = from;
    packet.id = id;
    packet.hop_limit = 3;
    packet.hop_start = 3;
    TEST_ASSERT_TRUE(buildInjectedText(packet, to, 0, text.c_str(), text.size()));
    return packet;
}

meshtastic_MeshPacket ourMirroredPacket(const std::string &text)
{
    return textPacket(kUs, NODENUM_BROADCAST, text);
}

std::vector<uint8_t> ourFrame(const meshtastic_MeshPacket &packet)
{
    return encodeMeshtasticFrame(packet, primaryChannelKey(), primaryChannelHash());
}

MeshcoreContact makeContact(const char *name, uint8_t seed)
{
    MeshcoreContact contact;
    for (size_t i = 0; i < kMeshcorePubKeySize; i++)
        contact.pubKey[i] = static_cast<uint8_t>(i * seed + 3);
    strncpy(contact.name, name, sizeof(contact.name) - 1);
    contact.nodeNum = nodeNumFromPubKey(contact.pubKey);
    contact.used = true;
    return contact;
}

} // namespace

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    channelFile = meshtastic_ChannelFile_init_zero;
    owner = meshtastic_User_init_zero;
    myNodeInfo.my_node_num = kUs;

    mockNodeDB = new NodeDB();
    nodeDB = mockNodeDB;
    myNodeInfo.my_node_num = kUs;

    mockService = new MockMeshService();
    service = mockService;

    channels.initDefaults();
    channels.onConfigChanged();

    mockRouter = new RecordingRouter();
    mockRouter->addInterface(std::unique_ptr<RadioInterface>(new RecordingRadioInterface()));
    router = mockRouter;

    sink = new FakeMeshcoreSink();
    mirrorModule = new MirrorModule();
    mirrorModule->setSink(sink);
    meshCompromiseOutboundHook = &MirrorModule::outboundHook;
}

void tearDown(void)
{
    meshCompromiseOutboundHook = nullptr;

    sink->groupTexts.clear();
    sink->directTexts.clear();
    sink->contacts.clear();
    sink->groupTexts.shrink_to_fit();
    sink->directTexts.shrink_to_fit();
    sink->contacts.shrink_to_fit();

    delete mirrorModule;
    mirrorModule = nullptr;

    delete sink;
    sink = nullptr;

    while (auto *status = mockService->getQueueStatusForPhone())
        mockService->releaseQueueStatusToPool(status);
    while (auto *toPhone = mockService->getForPhone())
        mockService->releaseToPool(toPhone);
    delete mockService;
    mockService = nullptr;
    service = nullptr;

    mockRouter->releasePending();
    delete mockRouter;
    mockRouter = nullptr;
    router = nullptr;

    delete mockNodeDB;
    mockNodeDB = nullptr;
    nodeDB = nullptr;
}

void test_channel_hash_matches_upstream(void)
{
    TEST_ASSERT_EQUAL_INT16(channels.getHash(0), primaryChannelHash());
}

void test_header_is_read_correctly_by_upstreams_struct(void)
{
    const meshtastic_MeshPacket packet = ourMirroredPacket("header check");
    const std::vector<uint8_t> frame = ourFrame(packet);
    TEST_ASSERT_TRUE(frame.size() > sizeof(PacketHeader));

    PacketHeader header;
    memcpy(&header, frame.data(), sizeof(PacketHeader));

    TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, header.to);
    TEST_ASSERT_EQUAL_UINT32(kUs, header.from);
    TEST_ASSERT_EQUAL_UINT32(kPacketId, header.id);
    TEST_ASSERT_EQUAL_UINT8(channels.getHash(0), header.channel);
    TEST_ASSERT_EQUAL_UINT8(3, header.flags & PACKET_FLAGS_HOP_LIMIT_MASK);
    TEST_ASSERT_EQUAL_UINT8(3, (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT);
}

void test_header_length_matches_upstream(void)
{
    TEST_ASSERT_EQUAL_UINT32(sizeof(PacketHeader), MESHTASTIC_HEADER_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(sizeof(PacketHeader), kMeshtasticHeaderLength);
}

void test_ciphertext_is_byte_identical_to_upstreams(void)
{
    const std::string text = "MC01: from the other mesh";
    const meshtastic_MeshPacket packet = ourMirroredPacket(text);
    const std::vector<uint8_t> frame = ourFrame(packet);
    TEST_ASSERT_TRUE(frame.size() > sizeof(PacketHeader));

    uint8_t expected[MAX_LORA_PAYLOAD_LEN] = {0};
    pb_ostream_t stream = pb_ostream_from_buffer(expected, sizeof(expected));
    TEST_ASSERT_TRUE(pb_encode(&stream, &meshtastic_Data_msg, &packet.decoded));
    const size_t length = stream.bytes_written;

    TEST_ASSERT_EQUAL_UINT32(frame.size() - sizeof(PacketHeader), length);

    const int16_t hash = channels.setActiveByIndex(0);
    TEST_ASSERT_TRUE(hash >= 0);
    crypto->encryptPacket(packet.from, packet.id, length, expected);

    TEST_ASSERT_EQUAL_MEMORY(expected, frame.data() + sizeof(PacketHeader), length);
}

void test_upstream_receives_our_mirrored_text(void)
{
    const std::string text = "MC01: hello meshtastic";
    const std::vector<uint8_t> frame = ourFrame(ourMirroredPacket(text));
    TEST_ASSERT_TRUE(frame.size() > sizeof(PacketHeader));

    PacketHeader header;
    memcpy(&header, frame.data(), sizeof(PacketHeader));

    const size_t length = frame.size() - sizeof(PacketHeader);
    uint8_t bytes[MAX_LORA_PAYLOAD_LEN] = {0};
    memcpy(bytes, frame.data() + sizeof(PacketHeader), length);

    TEST_ASSERT_TRUE(channels.decryptForHash(0, header.channel));
    crypto->decrypt(header.from, header.id, length, bytes);

    meshtastic_Data decoded = meshtastic_Data_init_default;
    pb_istream_t stream = pb_istream_from_buffer(bytes, length);
    TEST_ASSERT_TRUE(pb_decode(&stream, &meshtastic_Data_msg, &decoded));

    TEST_ASSERT_EQUAL(meshtastic_PortNum_TEXT_MESSAGE_APP, decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(text.size(), decoded.payload.size);
    TEST_ASSERT_EQUAL_MEMORY(text.data(), decoded.payload.bytes, text.size());
}

void test_contact_node_numbers_are_legal_destinations(void)
{
    uint8_t key[32] = {0};
    for (int i = 0; i < 32; i++)
        key[i] = static_cast<uint8_t>(i * 7 + 3);

    const uint32_t nodeNum = nodeNumFromPubKey(key);

    TEST_ASSERT_NOT_EQUAL(0u, nodeNum);
    TEST_ASSERT_NOT_EQUAL(NODENUM_BROADCAST, nodeNum);
    TEST_ASSERT_NOT_EQUAL(NODENUM_BROADCAST_NO_LORA, nodeNum);
    TEST_ASSERT_FALSE(isBroadcast(nodeNum));
}

void test_upstream_receives_our_bridged_direct_message(void)
{
    const MeshcoreContact contact = makeContact("bob", 11);

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.from = kUs;
    packet.id = kPacketId;
    const std::string text = "direct to a meshcore contact";
    TEST_ASSERT_TRUE(buildInjectedText(packet, contact.nodeNum, 0, text.c_str(), text.size()));

    TEST_ASSERT_EQUAL_UINT32(contact.nodeNum, packet.to);
    TEST_ASSERT_FALSE(isBroadcast(packet.to));

    const std::vector<uint8_t> frame = ourFrame(packet);
    PacketHeader header;
    memcpy(&header, frame.data(), sizeof(PacketHeader));
    TEST_ASSERT_EQUAL_UINT32(contact.nodeNum, header.to);

    const size_t length = frame.size() - sizeof(PacketHeader);
    uint8_t bytes[MAX_LORA_PAYLOAD_LEN] = {0};
    memcpy(bytes, frame.data() + sizeof(PacketHeader), length);

    TEST_ASSERT_TRUE(channels.decryptForHash(0, header.channel));
    crypto->decrypt(header.from, header.id, length, bytes);

    meshtastic_Data decoded = meshtastic_Data_init_default;
    pb_istream_t stream = pb_istream_from_buffer(bytes, length);
    TEST_ASSERT_TRUE(pb_decode(&stream, &meshtastic_Data_msg, &decoded));
    TEST_ASSERT_EQUAL_MEMORY(text.data(), decoded.payload.bytes, text.size());
}

void test_a_maximum_payload_fits_one_frame(void)
{
    const std::string text(kMeshtasticMaxText, 'x');
    const std::vector<uint8_t> frame = ourFrame(ourMirroredPacket(text));

    TEST_ASSERT_TRUE(frame.size() > 0);
    TEST_ASSERT_TRUE(frame.size() <= MAX_LORA_PAYLOAD_LEN);
}

void test_the_module_dispatcher_hides_local_packets_without_loopback(void)
{
    ProbeModule deaf("probe-no-loopback", true, false);
    ProbeModule listening("probe-loopback", true, true);

    meshtastic_MeshPacket packet = ourMirroredPacket("locally composed");
    MeshModule::callModules(packet, RX_SRC_LOCAL);

    TEST_ASSERT_EQUAL_UINT32(0, deaf.seen);
    TEST_ASSERT_EQUAL_UINT32(1, listening.seen);
}

void test_we_do_not_rely_on_the_loopback_gate_at_all(void)
{
    meshtastic_MeshPacket packet = ourMirroredPacket("mirror me");
    MeshModule::callModules(packet, RX_SRC_LOCAL);

    TEST_ASSERT_EQUAL_UINT32(0, sink->groupTexts.size());
}

void test_a_locally_composed_broadcast_is_mirrored_to_meshcore(void)
{
    strncpy(owner.short_name, "abcd", sizeof(owner.short_name) - 1);

    meshtastic_MeshPacket packet = ourMirroredPacket("over to meshcore");
    TEST_ASSERT_FALSE(MirrorModule::outboundHook(&packet));

    TEST_ASSERT_EQUAL_UINT32(1, sink->groupTexts.size());
    TEST_ASSERT_EQUAL_STRING("abcd", sink->groupTexts[0].sender.c_str());
    TEST_ASSERT_EQUAL_STRING("over to meshcore", sink->groupTexts[0].text.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, mirrorModule->mirror().mirroredCount());
}

void test_a_phone_composed_broadcast_with_from_zero_is_mirrored(void)
{
    // Router::sendLocal() calls meshCompromiseOutboundHook(p) before Router::send()
    // normalizes p->from via getFrom() (that happens later, inside send()). A phone
    // or on-device compose leaves p->from at the 0 sentinel at hook time, so this is
    // the packet shape mirrorBroadcast() actually sees for our own outgoing text -
    // ourMirroredPacket()'s pre-stamped kUs above never exercises that path.
    strncpy(owner.short_name, "abcd", sizeof(owner.short_name) - 1);

    meshtastic_MeshPacket packet = textPacket(0, NODENUM_BROADCAST, "from the phone");
    TEST_ASSERT_FALSE(MirrorModule::outboundHook(&packet));

    TEST_ASSERT_EQUAL_UINT32(1, sink->groupTexts.size());
    TEST_ASSERT_EQUAL_STRING("from the phone", sink->groupTexts[0].text.c_str());
}

void test_a_mirrored_broadcast_still_reaches_the_meshtastic_radio(void)
{
    meshtastic_MeshPacket *packet = router->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    const std::string text = "both networks";
    TEST_ASSERT_TRUE(buildInjectedText(*packet, NODENUM_BROADCAST, 0, text.c_str(), text.size()));
    packet->from = kUs;

    router->sendLocal(packet, RX_SRC_LOCAL);

    TEST_ASSERT_EQUAL_UINT32(1, sink->groupTexts.size());
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sent.size());
}

void test_someone_elses_broadcast_is_not_mirrored(void)
{
    meshtastic_MeshPacket packet = textPacket(kStranger, NODENUM_BROADCAST, "not mine");
    MeshModule::callModules(packet, RX_SRC_RADIO);

    TEST_ASSERT_EQUAL_UINT32(0, sink->groupTexts.size());
}

void test_a_mirrored_message_does_not_bounce_back(void)
{
    meshtastic_MeshPacket packet = ourMirroredPacket("only once");
    MirrorModule::outboundHook(&packet);
    MirrorModule::outboundHook(&packet);

    TEST_ASSERT_EQUAL_UINT32(1, sink->groupTexts.size());
}

void test_the_router_hands_every_outgoing_broadcast_to_the_hook(void)
{
    meshtastic_MeshPacket *packet = router->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    const std::string text = "through the real router";
    TEST_ASSERT_TRUE(buildInjectedText(*packet, NODENUM_BROADCAST, 0, text.c_str(), text.size()));
    packet->from = kUs;

    router->sendLocal(packet, RX_SRC_LOCAL);

    TEST_ASSERT_EQUAL_UINT32(1, sink->groupTexts.size());
    TEST_ASSERT_EQUAL_STRING(text.c_str(), sink->groupTexts[0].text.c_str());
}

void test_a_meshcore_contact_becomes_a_node_in_the_node_list(void)
{
    const MeshcoreContact contact = makeContact("alice", 5);

    mirrorModule->onMeshcoreContact(contact);

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(contact.nodeNum);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_STRING("alice", node->long_name);

    TEST_ASSERT_TRUE(sizeof(node->short_name) <= sizeof(contact.name));
    TEST_ASSERT_EQUAL_UINT32(sizeof(node->short_name) - 1, strlen(node->short_name));
    TEST_ASSERT_EQUAL_MEMORY("alice", node->short_name, sizeof(node->short_name) - 1);

    TEST_ASSERT_EQUAL_UINT32(1, mirrorModule->contactsLearned());
}

void test_a_direct_message_to_a_meshcore_contact_is_claimed(void)
{
    const MeshcoreContact contact = makeContact("carol", 13);
    sink->contacts.push_back(contact);
    mirrorModule->onMeshcoreContact(contact);

    meshtastic_MeshPacket *packet = router->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    const std::string text = "psst, over meshcore";
    TEST_ASSERT_TRUE(buildInjectedText(*packet, contact.nodeNum, 0, text.c_str(), text.size()));
    packet->from = kUs;

    TEST_ASSERT_TRUE(MirrorModule::outboundHook(packet));
    TEST_ASSERT_EQUAL_UINT32(1, sink->directTexts.size());
    TEST_ASSERT_EQUAL_UINT32(contact.nodeNum, sink->directTexts[0].nodeNum);
    TEST_ASSERT_EQUAL_STRING(text.c_str(), sink->directTexts[0].text.c_str());
}

void test_a_direct_message_to_an_ordinary_node_is_left_alone(void)
{
    meshtastic_MeshPacket *packet = router->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    TEST_ASSERT_TRUE(buildInjectedText(*packet, kStranger, 0, "stay on meshtastic", 18));
    packet->from = kUs;

    TEST_ASSERT_FALSE(MirrorModule::outboundHook(packet));
    TEST_ASSERT_EQUAL_UINT32(0, sink->directTexts.size());
    packetPool.release(packet);
}

void test_a_broadcast_is_never_claimed_for_meshcore(void)
{
    meshtastic_MeshPacket *packet = router->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    TEST_ASSERT_TRUE(buildInjectedText(*packet, NODENUM_BROADCAST, 0, "everyone", 8));
    packet->from = kUs;

    TEST_ASSERT_FALSE(MirrorModule::outboundHook(packet));
    TEST_ASSERT_EQUAL_UINT32(0, sink->directTexts.size());
    packetPool.release(packet);
}

void test_the_router_hands_a_contact_direct_message_to_meshcore(void)
{
    const MeshcoreContact contact = makeContact("dave", 17);
    sink->contacts.push_back(contact);

    meshtastic_MeshPacket *packet = router->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    const std::string text = "claimed before the radio";
    TEST_ASSERT_TRUE(buildInjectedText(*packet, contact.nodeNum, 0, text.c_str(), text.size()));
    packet->from = kUs;

    const ErrorCode code = router->sendLocal(packet, RX_SRC_LOCAL);

    TEST_ASSERT_EQUAL(ERRNO_SHOULD_RELEASE, code);
    TEST_ASSERT_EQUAL_UINT32(1, sink->directTexts.size());
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sent.size());
}

void test_an_inbound_meshcore_direct_message_becomes_a_meshtastic_dm(void)
{
    const MeshcoreContact contact = makeContact("erin", 19);
    mirrorModule->onMeshcoreContact(contact);

    const std::string text = "hello from meshcore";
    mirrorModule->onMeshcoreDirectText(contact, text.c_str(), text.size());

    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sent.size());

    meshtastic_MeshPacket *delivered = mockService->getForPhone();
    TEST_ASSERT_NOT_NULL(delivered);
    TEST_ASSERT_EQUAL_UINT32(contact.nodeNum, delivered->from);
    TEST_ASSERT_EQUAL_UINT32(kUs, delivered->to);
    TEST_ASSERT_EQUAL(meshtastic_PortNum_TEXT_MESSAGE_APP, delivered->decoded.portnum);
    TEST_ASSERT_EQUAL_MEMORY(text.data(), delivered->decoded.payload.bytes, text.size());
    mockService->releaseToPool(delivered);

    TEST_ASSERT_EQUAL_UINT32(1, mirrorModule->directsBridgedCount());
}

void test_an_inbound_meshcore_group_text_becomes_a_meshtastic_broadcast(void)
{
    const std::string text = "MC01: group traffic";
    mirrorModule->onMeshcoreText(text.c_str(), text.size());

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sent.size());
    const meshtastic_MeshPacket &sent = mockRouter->sent[0];
    TEST_ASSERT_TRUE(isBroadcast(sent.to));
    TEST_ASSERT_EQUAL_MEMORY(text.data(), sent.decoded.payload.bytes, text.size());
}

void test_an_injected_meshcore_message_is_not_mirrored_straight_back(void)
{
    const std::string text = "MC01: no echo please";
    mirrorModule->onMeshcoreText(text.c_str(), text.size());

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sent.size());
    meshtastic_MeshPacket echo = mockRouter->sent[0];
    echo.from = kUs;

    MirrorModule::outboundHook(&echo);

    TEST_ASSERT_EQUAL_UINT32(0, sink->groupTexts.size());
}

void test_an_announcement_reaches_meshtastic_without_returning_to_meshcore(void)
{
    const std::string text = "up 5m, 12 heard, 3 mirrored";
    TEST_ASSERT_TRUE(mirrorModule->announce(text.c_str(), text.size()));

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sent.size());
    const meshtastic_MeshPacket &sent = mockRouter->sent[0];
    TEST_ASSERT_TRUE(isBroadcast(sent.to));
    TEST_ASSERT_EQUAL_MEMORY(text.data(), sent.decoded.payload.bytes, text.size());

    meshtastic_MeshPacket echo = sent;
    echo.from = kUs;
    MirrorModule::outboundHook(&echo);
    TEST_ASSERT_EQUAL_UINT32(0, sink->groupTexts.size());
}

void test_nothing_is_injected_while_the_router_is_missing(void)
{
    Router *saved = router;
    router = nullptr;

    const std::string text = "MC01: nowhere to go";
    mirrorModule->onMeshcoreText(text.c_str(), text.size());

    router = saved;
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sent.size());
}

void test_nothing_is_injected_while_the_service_is_missing(void)
{
    MeshService *saved = service;
    service = nullptr;

    const std::string text = "MC01: no service";
    mirrorModule->onMeshcoreText(text.c_str(), text.size());

    service = saved;
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sent.size());
}

void test_an_unencodable_meshcore_text_is_dropped_not_injected(void)
{
    const std::string garbage(meshtastic_Constants_DATA_PAYLOAD_LEN + 4, '\x80');

    mirrorModule->onMeshcoreText(garbage.c_str(), garbage.size());

    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sent.size());
}

void test_reverse_mirroring_off_drops_meshcore_traffic_both_ways(void)
{
    MirrorConfig config;
    config.reverseEnabled = false;
    mirrorModule->setConfig(config);

    const MeshcoreContact contact = makeContact("frank", 23);
    mirrorModule->onMeshcoreContact(contact);

    const std::string text = "MC01: should not appear";
    mirrorModule->onMeshcoreText(text.c_str(), text.size());
    mirrorModule->onMeshcoreDirectText(contact, text.c_str(), text.size());

    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sent.size());
    TEST_ASSERT_NULL(mockService->getForPhone());
    TEST_ASSERT_EQUAL_UINT32(0, mirrorModule->directsBridgedCount());
}

void test_a_meshcore_contact_is_not_learned_without_a_node_list(void)
{
    NodeDB *saved = nodeDB;
    nodeDB = nullptr;

    mirrorModule->onMeshcoreContact(makeContact("grace", 29));

    nodeDB = saved;
    TEST_ASSERT_EQUAL_UINT32(0, mirrorModule->contactsLearned());
}

void test_a_broadcast_is_not_mirrored_without_a_meshcore_sink(void)
{
    mirrorModule->setSink(nullptr);

    const meshtastic_MeshPacket packet = ourMirroredPacket("into the void");
    mirrorModule->handleReceived(packet);

    mirrorModule->setSink(sink);
    TEST_ASSERT_EQUAL_UINT32(0, sink->groupTexts.size());
}

void test_a_failed_meshcore_send_does_not_stop_meshtastic(void)
{
    sink->groupTextResult = false;

    meshtastic_MeshPacket *packet = router->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    const std::string text = "meshcore is deaf today";
    TEST_ASSERT_TRUE(buildInjectedText(*packet, NODENUM_BROADCAST, 0, text.c_str(), text.size()));
    packet->from = kUs;

    const ErrorCode code = router->sendLocal(packet, RX_SRC_LOCAL);

    sink->groupTextResult = true;
    TEST_ASSERT_EQUAL(ERRNO_OK, code);
    TEST_ASSERT_EQUAL_UINT32(1, sink->groupTexts.size());
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sent.size());
}

void test_a_failed_meshcore_direct_send_still_claims_the_packet(void)
{
    sink->directTextResult = false;
    const MeshcoreContact contact = makeContact("heidi", 31);
    sink->contacts.push_back(contact);

    meshtastic_MeshPacket *packet = router->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    const std::string text = "swallowed";
    TEST_ASSERT_TRUE(buildInjectedText(*packet, contact.nodeNum, 0, text.c_str(), text.size()));
    packet->from = kUs;

    const bool claimed = mirrorModule->handleOutbound(packet);

    sink->directTextResult = true;
    TEST_ASSERT_TRUE(claimed);
    TEST_ASSERT_EQUAL_UINT32(1, sink->directTexts.size());
    TEST_ASSERT_EQUAL_UINT32(0, mirrorModule->directsBridgedCount());
}

void test_every_upstream_key_maps_to_the_navigation_key_we_expect(void)
{
    struct Mapping {
        input_broker_event event;
        UiKey key;
    };

    const Mapping mappings[] = {
        {INPUT_BROKER_CANCEL, UiKey::Cancel},      {INPUT_BROKER_BACK, UiKey::Cancel},
        {INPUT_BROKER_SELECT_LONG, UiKey::SelectLong}, {INPUT_BROKER_SELECT, UiKey::Select},
        {INPUT_BROKER_UP, UiKey::Up},              {INPUT_BROKER_DOWN, UiKey::Down},
        {INPUT_BROKER_LEFT, UiKey::Left},          {INPUT_BROKER_RIGHT, UiKey::Right},
    };

    for (const Mapping &mapping : mappings) {
        InputEvent event = {};
        event.inputEvent = mapping.event;
        TEST_ASSERT_EQUAL(static_cast<int>(mapping.key), static_cast<int>(uiKeyFor(&event)));
    }
}

void test_keys_the_bridge_does_not_use_are_ignored(void)
{
    InputEvent event = {};
    event.inputEvent = INPUT_BROKER_ANYKEY;
    TEST_ASSERT_EQUAL(static_cast<int>(UiKey::None), static_cast<int>(uiKeyFor(&event)));

    event.inputEvent = INPUT_BROKER_NONE;
    TEST_ASSERT_EQUAL(static_cast<int>(UiKey::None), static_cast<int>(uiKeyFor(&event)));

    TEST_ASSERT_EQUAL(static_cast<int>(UiKey::None), static_cast<int>(uiKeyFor(nullptr)));
}

void test_the_navigation_keys_are_all_distinct(void)
{
    const input_broker_event events[] = {INPUT_BROKER_SELECT, INPUT_BROKER_SELECT_LONG, INPUT_BROKER_UP,
                                         INPUT_BROKER_DOWN,   INPUT_BROKER_LEFT,        INPUT_BROKER_RIGHT};

    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        for (size_t j = i + 1; j < sizeof(events) / sizeof(events[0]); j++) {
            InputEvent first = {};
            InputEvent second = {};
            first.inputEvent = events[i];
            second.inputEvent = events[j];
            TEST_ASSERT_NOT_EQUAL(static_cast<int>(uiKeyFor(&first)), static_cast<int>(uiKeyFor(&second)));
        }
    }
}

void test_settings_survive_a_write_and_read_on_a_real_filesystem(void)
{
    BridgeSettings written = defaultSettings();
    written.meshcore.spreadingFactor = 9;
    written.meshcore.frequencyMhz = 868.5f;
    written.hopLimit = 5;
    written.txPowerDbm = 14;
    written.advertIntervalMinutes = 42;
    written.statsIntervalMinutes = 7;
    written.mirror.meshcoreChannel = 1;
    written.mirror.reverseEnabled = false;

    TEST_ASSERT_TRUE(saveSettings(written));

    BridgeSettings read = defaultSettings();
    TEST_ASSERT_TRUE(loadSettings(read));

    TEST_ASSERT_EQUAL_UINT8(9, read.meshcore.spreadingFactor);
    TEST_ASSERT_EQUAL_FLOAT(868.5f, read.meshcore.frequencyMhz);
    TEST_ASSERT_EQUAL_UINT8(5, read.hopLimit);
    TEST_ASSERT_EQUAL_INT8(14, read.txPowerDbm);
    TEST_ASSERT_EQUAL_UINT16(42, read.advertIntervalMinutes);
    TEST_ASSERT_EQUAL_UINT16(7, read.statsIntervalMinutes);
    TEST_ASSERT_EQUAL_UINT8(1, read.mirror.meshcoreChannel);
    TEST_ASSERT_FALSE(read.mirror.reverseEnabled);
}

void test_invalid_settings_are_never_written(void)
{
    BridgeSettings good = defaultSettings();
    good.hopLimit = 4;
    TEST_ASSERT_TRUE(saveSettings(good));

    BridgeSettings bad = defaultSettings();
    bad.meshcore.spreadingFactor = 99;
    TEST_ASSERT_FALSE(validateSettings(bad));
    TEST_ASSERT_FALSE(saveSettings(bad));

    BridgeSettings read = defaultSettings();
    TEST_ASSERT_TRUE(loadSettings(read));
    TEST_ASSERT_EQUAL_UINT8(4, read.hopLimit);
}

void test_a_settings_file_from_an_older_version_is_refused(void)
{
    BridgeSettings settings = defaultSettings();
    settings.hopLimit = 6;
    TEST_ASSERT_TRUE(saveSettings(settings));

    StoredSettings stale;
    stale.version = kSettingsVersion - 1;
    stale.settings = settings;

    auto file = FSCom.open("/meshcompromise/settings.bin", FILE_O_WRITE);
    TEST_ASSERT_TRUE(file);
    file.write(reinterpret_cast<const uint8_t *>(&stale), sizeof(stale));
    file.close();

    BridgeSettings read = defaultSettings();
    TEST_ASSERT_FALSE(loadSettings(read));
}

void test_a_settings_file_with_the_wrong_magic_is_refused(void)
{
    StoredSettings foreign;
    foreign.magic = 0xDEADBEEF;
    foreign.settings = defaultSettings();

    auto file = FSCom.open("/meshcompromise/settings.bin", FILE_O_WRITE);
    TEST_ASSERT_TRUE(file);
    file.write(reinterpret_cast<const uint8_t *>(&foreign), sizeof(foreign));
    file.close();

    BridgeSettings read = defaultSettings();
    TEST_ASSERT_FALSE(loadSettings(read));
}

void test_a_truncated_settings_file_is_refused(void)
{
    StoredSettings stored;
    stored.settings = defaultSettings();

    auto file = FSCom.open("/meshcompromise/settings.bin", FILE_O_WRITE);
    TEST_ASSERT_TRUE(file);
    file.write(reinterpret_cast<const uint8_t *>(&stored), sizeof(stored) / 2);
    file.close();

    BridgeSettings read = defaultSettings();
    TEST_ASSERT_FALSE(loadSettings(read));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    RUN_TEST(test_channel_hash_matches_upstream);
    RUN_TEST(test_header_length_matches_upstream);
    RUN_TEST(test_header_is_read_correctly_by_upstreams_struct);
    RUN_TEST(test_ciphertext_is_byte_identical_to_upstreams);
    RUN_TEST(test_upstream_receives_our_mirrored_text);
    RUN_TEST(test_contact_node_numbers_are_legal_destinations);
    RUN_TEST(test_upstream_receives_our_bridged_direct_message);
    RUN_TEST(test_a_maximum_payload_fits_one_frame);

    RUN_TEST(test_the_module_dispatcher_hides_local_packets_without_loopback);
    RUN_TEST(test_we_do_not_rely_on_the_loopback_gate_at_all);
    RUN_TEST(test_a_locally_composed_broadcast_is_mirrored_to_meshcore);
    RUN_TEST(test_a_phone_composed_broadcast_with_from_zero_is_mirrored);
    RUN_TEST(test_a_mirrored_broadcast_still_reaches_the_meshtastic_radio);
    RUN_TEST(test_someone_elses_broadcast_is_not_mirrored);
    RUN_TEST(test_a_mirrored_message_does_not_bounce_back);
    RUN_TEST(test_the_router_hands_every_outgoing_broadcast_to_the_hook);

    RUN_TEST(test_a_meshcore_contact_becomes_a_node_in_the_node_list);
    RUN_TEST(test_a_direct_message_to_a_meshcore_contact_is_claimed);
    RUN_TEST(test_a_direct_message_to_an_ordinary_node_is_left_alone);
    RUN_TEST(test_a_broadcast_is_never_claimed_for_meshcore);
    RUN_TEST(test_the_router_hands_a_contact_direct_message_to_meshcore);

    RUN_TEST(test_an_inbound_meshcore_direct_message_becomes_a_meshtastic_dm);
    RUN_TEST(test_an_inbound_meshcore_group_text_becomes_a_meshtastic_broadcast);
    RUN_TEST(test_an_injected_meshcore_message_is_not_mirrored_straight_back);
    RUN_TEST(test_an_announcement_reaches_meshtastic_without_returning_to_meshcore);

    RUN_TEST(test_nothing_is_injected_while_the_router_is_missing);
    RUN_TEST(test_nothing_is_injected_while_the_service_is_missing);
    RUN_TEST(test_an_unencodable_meshcore_text_is_dropped_not_injected);
    RUN_TEST(test_reverse_mirroring_off_drops_meshcore_traffic_both_ways);
    RUN_TEST(test_a_meshcore_contact_is_not_learned_without_a_node_list);
    RUN_TEST(test_a_broadcast_is_not_mirrored_without_a_meshcore_sink);
    RUN_TEST(test_a_failed_meshcore_send_does_not_stop_meshtastic);
    RUN_TEST(test_a_failed_meshcore_direct_send_still_claims_the_packet);

    RUN_TEST(test_every_upstream_key_maps_to_the_navigation_key_we_expect);
    RUN_TEST(test_keys_the_bridge_does_not_use_are_ignored);
    RUN_TEST(test_the_navigation_keys_are_all_distinct);

    RUN_TEST(test_settings_survive_a_write_and_read_on_a_real_filesystem);
    RUN_TEST(test_invalid_settings_are_never_written);
    RUN_TEST(test_a_settings_file_from_an_older_version_is_refused);
    RUN_TEST(test_a_settings_file_with_the_wrong_magic_is_refused);
    RUN_TEST(test_a_truncated_settings_file_is_refused);

    exit(UNITY_END());
}

void loop() {}
