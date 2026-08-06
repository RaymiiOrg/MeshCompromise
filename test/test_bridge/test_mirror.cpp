#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "meshcompromise/mirror.h"

using namespace meshcompromise;

namespace
{

constexpr uint32_t kLocalNode = 0xAABBCCDD;
constexpr uint32_t kRemoteNode = 0x11223344;

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
    config.enabled = true;
    config.policy = MirrorPolicy::LocalOnly;
    Mirror mirror(config);
    mirror.setLocalNode(kLocalNode);
    return mirror;
}

} // namespace

TEST(Mirror, EnabledByDefault)
{
    MirrorConfig config;
    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.policy, MirrorPolicy::AllBroadcasts);
}

TEST(Mirror, MirrorsLocalBroadcastText)
{
    Mirror mirror = makeMirror();
    EXPECT_EQ(mirror.evaluate(localBroadcast(1)), MirrorDecision::Send);
}

TEST(Mirror, SkipsWhenDisabled)
{
    MirrorConfig config;
    config.enabled = false;
    Mirror mirror(config);
    mirror.setLocalNode(kLocalNode);
    EXPECT_EQ(mirror.evaluate(localBroadcast(1)), MirrorDecision::NotEnabled);
}

TEST(Mirror, SkipsNonTextPackets)
{
    Mirror mirror = makeMirror();
    MirrorSource source = localBroadcast(1);
    source.isTextMessage = false;
    EXPECT_EQ(mirror.evaluate(source), MirrorDecision::NotText);
}

TEST(Mirror, SkipsDirectMessages)
{
    Mirror mirror = makeMirror();
    MirrorSource source = localBroadcast(1);
    source.isBroadcast = false;
    source.toNode = kRemoteNode;
    EXPECT_EQ(mirror.evaluate(source), MirrorDecision::NotBroadcast);
}

TEST(Mirror, LocalOnlyPolicySkipsRemoteTraffic)
{
    Mirror mirror = makeMirror();
    MirrorSource source = localBroadcast(1);
    source.fromNode = kRemoteNode;
    EXPECT_EQ(mirror.evaluate(source), MirrorDecision::NotLocal);
}

TEST(Mirror, AllBroadcastsPolicyAcceptsRemoteTraffic)
{
    MirrorConfig config;
    config.policy = MirrorPolicy::AllBroadcasts;
    Mirror mirror(config);
    mirror.setLocalNode(kLocalNode);
    MirrorSource source = localBroadcast(1);
    source.fromNode = kRemoteNode;
    EXPECT_EQ(mirror.evaluate(source), MirrorDecision::Send);
}

TEST(Mirror, SamePacketIsNeverMirroredTwice)
{
    Mirror mirror = makeMirror();
    MirrorMessage message;
    const std::string text = "hello mesh";

    EXPECT_EQ(mirror.prepare(localBroadcast(42), text.c_str(), text.size(), message), MirrorDecision::Send);
    EXPECT_EQ(mirror.prepare(localBroadcast(42), text.c_str(), text.size(), message), MirrorDecision::AlreadyMirrored);
    EXPECT_EQ(mirror.mirroredCount(), 1u);
}

TEST(Mirror, MirroredMessageComingBackDoesNotReMirror)
{
    Mirror mirror = makeMirror();
    MirrorMessage message;
    const std::string text = "loop me";

    ASSERT_EQ(mirror.prepare(localBroadcast(7), text.c_str(), text.size(), message), MirrorDecision::Send);

    MirrorSource echoed = localBroadcast(7);
    echoed.fromNode = kRemoteNode;
    EXPECT_NE(mirror.prepare(echoed, text.c_str(), text.size(), message), MirrorDecision::Send);
    EXPECT_EQ(mirror.mirroredCount(), 1u);
}

TEST(Mirror, DistinctPacketsAreEachMirrored)
{
    Mirror mirror = makeMirror();
    MirrorMessage message;
    const std::string text = "distinct";

    for (uint32_t id = 1; id <= 5; id++)
        EXPECT_EQ(mirror.prepare(localBroadcast(id), text.c_str(), text.size(), message), MirrorDecision::Send);

    EXPECT_EQ(mirror.mirroredCount(), 5u);
}

TEST(Mirror, EmptyPayloadIsRejected)
{
    Mirror mirror = makeMirror();
    MirrorMessage message;
    EXPECT_EQ(mirror.prepare(localBroadcast(1), "", 0, message), MirrorDecision::EmptyPayload);
    EXPECT_EQ(mirror.mirroredCount(), 0u);
}

TEST(Mirror, SuppressedCountTracksRejections)
{
    Mirror mirror = makeMirror();
    MirrorMessage message;
    MirrorSource source = localBroadcast(1);
    source.isTextMessage = false;
    mirror.prepare(source, "x", 1, message);
    EXPECT_EQ(mirror.suppressedCount(), 1u);
}

TEST(MirrorHistory, RingBufferForgetsOldestEntries)
{
    MirrorHistory history;
    for (uint32_t id = 0; id < kMirrorHistorySize * 2; id++)
        history.record(id);
    EXPECT_EQ(history.size(), kMirrorHistorySize);
    EXPECT_TRUE(history.contains(kMirrorHistorySize * 2 - 1));
    EXPECT_FALSE(history.contains(0));
}

TEST(MirrorHistory, DuplicateRecordDoesNotConsumeSlot)
{
    MirrorHistory history;
    for (int i = 0; i < 10; i++)
        history.record(99);
    EXPECT_EQ(history.size(), 1u);
}

TEST(MirrorPayload, ShortTextPassesThroughUnchanged)
{
    MirrorMessage message;
    const std::string text = "short";
    ASSERT_TRUE(buildMirrorMessage(text.c_str(), text.size(), 0, message));
    EXPECT_EQ(message.length, text.size());
    EXPECT_FALSE(message.truncated);
}

TEST(MirrorPayload, OversizedTextIsTruncatedToMeshcoreLimit)
{
    MirrorMessage message;
    const std::string text(300, 'a');
    ASSERT_TRUE(buildMirrorMessage(text.c_str(), text.size(), 0, message));
    EXPECT_EQ(message.length, kMeshcoreMaxPayload);
    EXPECT_TRUE(message.truncated);
}

TEST(MirrorPayload, TruncationDoesNotSplitMultibyteCharacters)
{
    std::string text;
    while (text.size() < 300)
        text += "\xE2\x82\xAC";

    MirrorMessage message;
    ASSERT_TRUE(buildMirrorMessage(text.c_str(), text.size(), 0, message));
    EXPECT_LE(message.length, kMeshcoreMaxPayload);
    EXPECT_EQ(message.length % 3, 0u);
}

TEST(MirrorPayload, ChannelIsCarriedThrough)
{
    MirrorMessage message;
    const std::string text = "channelled";
    ASSERT_TRUE(buildMirrorMessage(text.c_str(), text.size(), 3, message));
    EXPECT_EQ(message.channel, 3);
}

TEST(MirrorPayload, NullTextIsRejected)
{
    MirrorMessage message;
    EXPECT_FALSE(buildMirrorMessage(nullptr, 10, 0, message));
}

TEST(ReverseMirror, EnabledByDefault)
{
    MirrorConfig config;
    EXPECT_TRUE(config.reverseEnabled);
}

TEST(ReverseMirror, GroupTextRoundTripsBackToPlainText)
{
    GroupTextPayload payload;
    const std::string text = "hello from meshcore";
    ASSERT_TRUE(buildGroupTextPayload(0x1234, "core", text.c_str(), text.size(), payload));

    char out[kMeshtasticMaxText + 1] = {0};
    const size_t length = extractGroupText(payload.bytes, payload.length, out, sizeof(out));
    ASSERT_GT(length, 0u);
    EXPECT_EQ(std::string(out, length), "core: " + text);
}

TEST(ReverseMirror, ExtractRejectsHeaderOnlyPayload)
{
    const uint8_t payload[kGroupTextHeader] = {0};
    char out[32] = {0};
    EXPECT_EQ(extractGroupText(payload, sizeof(payload), out, sizeof(out)), 0u);
}

TEST(ReverseMirror, ExtractRejectsNonPlainTextType)
{
    uint8_t payload[kGroupTextHeader + 4] = {0};
    payload[4] = 1 << 2;
    payload[kGroupTextHeader] = 'h';
    payload[kGroupTextHeader + 1] = 'i';

    char out[32] = {0};
    EXPECT_EQ(extractGroupText(payload, sizeof(payload), out, sizeof(out)), 0u);
}

TEST(ReverseMirror, ExtractStopsAtTerminator)
{
    uint8_t payload[kGroupTextHeader + 8] = {0};
    memcpy(&payload[kGroupTextHeader], "ab\0cdef", 7);

    char out[32] = {0};
    const size_t length = extractGroupText(payload, sizeof(payload), out, sizeof(out));
    EXPECT_EQ(length, 2u);
    EXPECT_STREQ(out, "ab");
}

TEST(ReverseMirror, ExtractRespectsOutputCapacity)
{
    GroupTextPayload payload;
    const std::string text(140, 'z');
    ASSERT_TRUE(buildGroupTextPayload(1, "n", text.c_str(), text.size(), payload));

    char out[16] = {0};
    const size_t length = extractGroupText(payload.bytes, payload.length, out, sizeof(out));
    EXPECT_EQ(length, sizeof(out) - 1);
    EXPECT_EQ(out[sizeof(out) - 1], '\0');
}

TEST(ReverseMirror, ExtractDoesNotSplitMultibyteCharacters)
{
    std::string text;
    while (text.size() < 60)
        text += "\xE2\x82\xAC";

    GroupTextPayload payload;
    ASSERT_TRUE(buildGroupTextPayload(1, "ab", text.c_str(), text.size(), payload));

    char out[19] = {0};
    const size_t length = extractGroupText(payload.bytes, payload.length, out, sizeof(out));
    EXPECT_EQ(length, 16u);
    EXPECT_EQ(std::string(out, length), "ab: \xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC");
}

TEST(ReverseMirror, InjectedPacketIsNotMirroredBack)
{
    Mirror mirror = makeMirror();
    MirrorMessage message;
    const std::string text = "core: hi";

    mirror.noteInjected(0x5150);
    EXPECT_EQ(mirror.injectedCount(), 1u);

    MirrorSource echoed = localBroadcast(0x5150);
    EXPECT_EQ(mirror.prepare(echoed, text.c_str(), text.size(), message), MirrorDecision::AlreadyMirrored);
    EXPECT_EQ(mirror.mirroredCount(), 0u);
}

TEST(ReverseMirror, InjectedMarkerDoesNotBlockOtherPackets)
{
    Mirror mirror = makeMirror();
    MirrorMessage message;
    const std::string text = "unrelated";

    mirror.noteInjected(0x1111);
    EXPECT_EQ(mirror.prepare(localBroadcast(0x2222), text.c_str(), text.size(), message), MirrorDecision::Send);
}

TEST(ReverseMirror, ResetCountersClearsInjected)
{
    Mirror mirror = makeMirror();
    mirror.noteInjected(1);
    mirror.resetCounters();
    EXPECT_EQ(mirror.injectedCount(), 0u);
}
