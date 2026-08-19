#include <gtest/gtest.h>

#include <string>

#include "host_mesh.h"

#include "meshcompromise/base64.h"
#include "meshcompromise/meshcore_radio.h"
#include "meshcompromise/mirror.h"
#include "meshcompromise/ui_text.h"

using namespace meshcompromise;

namespace
{

class ExposedStack : public MeshcoreStack
{
  public:
    using MeshcoreStack::MeshcoreStack;
    using MeshcoreStack::getPeerSharedSecret;
    using MeshcoreStack::onPeerDataRecv;
};

} // namespace

TEST(Base64Gaps, ThePlusAndSlashAlphabetDecodes)
{
    uint8_t out[8] = {0};

    const size_t written = decodeBase64("+/+/", out, sizeof(out));

    ASSERT_EQ(3u, written);
    EXPECT_EQ(0xFB, out[0]);
    EXPECT_EQ(0xFF, out[1]);
    EXPECT_EQ(0xBF, out[2]);
}

TEST(Base64Gaps, EveryAlphabetRangeIsAccepted)
{
    uint8_t out[16] = {0};

    EXPECT_GT(decodeBase64("QUJD", out, sizeof(out)), 0u);
    EXPECT_GT(decodeBase64("YWJj", out, sizeof(out)), 0u);
    EXPECT_GT(decodeBase64("MDEy", out, sizeof(out)), 0u);
}

TEST(MirrorGaps, ReconfiguringAtRuntimeTakesEffect)
{
    Mirror mirror{MirrorConfig()};
    mirror.setLocalNode(0xAABBCCDD);

    MirrorConfig off;
    off.enabled = false;
    mirror.setConfig(off);

    MirrorSource source;
    source.packetId = 1;
    source.fromNode = 0xAABBCCDD;
    source.toNode = 0xFFFFFFFFu;
    source.isTextMessage = true;
    source.isBroadcast = true;

    MirrorMessage message;
    EXPECT_EQ(MirrorDecision::NotEnabled, mirror.prepare(source, "nope", 4, message));

    MirrorConfig on;
    mirror.setConfig(on);
    EXPECT_EQ(MirrorDecision::Send, mirror.prepare(source, "yes", 3, message));
}

TEST(UiTextGaps, AnOutOfRangeFieldRendersEmptyRatherThanGarbage)
{
    const SettingField bogus = static_cast<SettingField>(static_cast<int>(SettingField::Count) + 5);

    EXPECT_STREQ("", settingLabel(bogus));

    BridgeSettings settings = defaultSettings();
    char value[32] = {'x', 0};
    settingValue(settings, bogus, value, sizeof(value));
    EXPECT_STREQ("", value);
}

TEST(MeshcoreRadioGaps, TheRadioForwardsConfigurationToTheArbiter)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    MeshcoreRadio radio(driver, host);

    LoraProfile profile = meshcoreDefaultProfile();
    profile.syncWord = 0x66;
    radio.setMeshcoreProfile(profile);
    radio.setTxPower(14);

    radio.arbiter().enterMeshcore(SwitchMode::Aligned, profile);

    EXPECT_EQ(0x66, driver.active.syncWord);
}

TEST(MeshcoreRadioGaps, TheNoiseFloorComesFromTheHost)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    MeshcoreRadio radio(driver, host);

    EXPECT_EQ(-120, radio.getNoiseFloor());
}

TEST(MeshcoreRadioGaps, FinishingASendIsHarmless)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    MeshcoreRadio radio(driver, host);

    radio.onSendFinished();

    SUCCEED();
}

TEST(MeshcoreRadioGaps, AnEmptyReceiveRequestReadsNothing)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    MeshcoreRadio radio(driver, host);

    uint8_t buffer[4] = {0};

    EXPECT_EQ(0, radio.recvRaw(nullptr, sizeof(buffer)));
    EXPECT_EQ(0, radio.recvRaw(buffer, 0));
    EXPECT_EQ(0, radio.recvRaw(buffer, -1));
}

TEST(StackGaps, AnAdvertWithoutANameGetsAKeyDerivedOne)
{
    hostMillis = 0;
    HostNode bridge(1);
    HostNode nameless(7);
    VirtualAir air;
    bridge.takeRadio();
    nameless.takeRadio();

    ASSERT_TRUE(nameless.stack.sendAdvert(""));
    nameless.pump();

    const std::vector<uint8_t> frame = lastFrame(nameless.driver);
    ASSERT_FALSE(frame.empty());

    carryOverAir(air, bridge, frame);
    bridge.pump();

    ASSERT_EQ(1u, bridge.stack.contactCount());
    ASSERT_EQ(1u, bridge.collector.contacts.size());
    EXPECT_GT(strlen(bridge.collector.contacts[0].name), 0u);
}

TEST(StackGaps, APeerIndexOutsideTheMatchTableIsRefused)
{
    hostMillis = 0;
    HostNode node(3);

    HostClock clock;
    HostRtcClock rtc;
    HostRng rng(11);
    SimpleMeshTables tables;
    StaticPoolPacketManager manager(4);
    ExposedStack stack(node.radio, clock, rng, rtc, manager, tables);
    stack.self_id = mesh::LocalIdentity(&rng);
    stack.begin();

    uint8_t secret[32];
    memset(secret, 0xAA, sizeof(secret));

    stack.getPeerSharedSecret(secret, -1);
    stack.getPeerSharedSecret(secret, 99);

    for (size_t i = 0; i < sizeof(secret); i++)
        EXPECT_EQ(0xAA, secret[i]);
}

TEST(StackGaps, PeerDataFromAnUnknownSenderIsDropped)
{
    hostMillis = 0;
    HostNode node(4);

    HostClock clock;
    HostRtcClock rtc;
    HostRng rng(12);
    SimpleMeshTables tables;
    StaticPoolPacketManager manager(4);
    ExposedStack stack(node.radio, clock, rng, rtc, manager, tables);
    TextCollector collector;
    stack.self_id = mesh::LocalIdentity(&rng);
    stack.setTextSink(&collector);
    stack.begin();

    uint8_t payload[16] = {0};
    stack.onPeerDataRecv(nullptr, kMeshcorePayloadTxtMsg, -1, nullptr, payload, sizeof(payload));
    stack.onPeerDataRecv(nullptr, kMeshcorePayloadTxtMsg, 42, nullptr, payload, sizeof(payload));
    stack.onPeerDataRecv(nullptr, 0x99, 0, nullptr, payload, sizeof(payload));

    EXPECT_TRUE(collector.directs.empty());
}
