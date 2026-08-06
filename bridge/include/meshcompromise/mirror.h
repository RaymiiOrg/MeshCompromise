#pragma once

#include <cstddef>
#include <cstdint>

namespace meshcompromise
{

constexpr size_t kMeshcoreMaxPayload = 184;
constexpr size_t kMirrorHistorySize = 32;

enum class MirrorPolicy { LocalOnly, AllBroadcasts };

enum class MirrorDecision { Send, NotEnabled, NotBroadcast, NotText, NotLocal, AlreadyMirrored, EmptyPayload };

struct MirrorSource {
    uint32_t packetId = 0;
    uint32_t fromNode = 0;
    uint32_t toNode = 0;
    uint8_t channel = 0;
    bool isTextMessage = false;
    bool isBroadcast = false;
};

struct MirrorConfig {
    bool enabled = true;
    bool reverseEnabled = true;
    MirrorPolicy policy = MirrorPolicy::AllBroadcasts;
    uint8_t meshcoreChannel = 0;
};

struct MirrorMessage {
    uint8_t payload[kMeshcoreMaxPayload] = {0};
    size_t length = 0;
    uint8_t channel = 0;
    bool truncated = false;
};

constexpr uint8_t kMeshcoreRouteFlood = 0x01;
constexpr uint8_t kMeshcorePayloadGrpTxt = 0x05;
constexpr uint8_t kMeshcorePayloadTxtMsg = 0x02;
constexpr uint8_t kMeshcoreTypeShift = 2;

constexpr size_t kMeshcoreMaxText = 160;
constexpr size_t kGroupTextHeader = 5;
constexpr uint8_t kGroupTextPlain = 0;
constexpr size_t kGroupTextPayloadMax = kGroupTextHeader + kMeshcoreMaxText + 1;

struct GroupTextPayload {
    uint8_t bytes[kGroupTextPayloadMax] = {0};
    size_t length = 0;
    bool truncated = false;
};

size_t truncateUtf8(const char *text, size_t length, size_t limit);

bool buildMirrorMessage(const char *text, size_t length, uint8_t channel, MirrorMessage &out);

uint8_t meshcoreHeaderByte(uint8_t routeType, uint8_t payloadType);

bool buildGroupTextPayload(uint32_t timestamp, const char *senderName, const char *text, size_t length,
                           GroupTextPayload &out);

constexpr size_t kMeshtasticMaxText = 233;

size_t extractGroupText(const uint8_t *payload, size_t length, char *out, size_t capacity);

bool buildDirectTextPayload(uint32_t timestamp, const char *text, size_t length, GroupTextPayload &out);

class MirrorHistory
{
  public:
    void record(uint32_t packetId);
    bool contains(uint32_t packetId) const;
    void clear();
    size_t size() const { return count_; }

  private:
    uint32_t entries_[kMirrorHistorySize] = {0};
    size_t next_ = 0;
    size_t count_ = 0;
};

class Mirror
{
  public:
    explicit Mirror(const MirrorConfig &config);

    void setConfig(const MirrorConfig &config);
    const MirrorConfig &config() const { return config_; }
    void setLocalNode(uint32_t nodeNum);

    MirrorDecision evaluate(const MirrorSource &source) const;

    MirrorDecision prepare(const MirrorSource &source, const char *text, size_t length, MirrorMessage &out);

    void noteInjected(uint32_t packetId);
    void suppress(uint32_t packetId);
    uint32_t injectedCount() const { return injected_; }

    uint32_t mirroredCount() const { return mirrored_; }
    uint32_t suppressedCount() const { return suppressed_; }
    const MirrorHistory &history() const { return history_; }
    void resetCounters();

  private:
    MirrorConfig config_;
    MirrorHistory history_;
    uint32_t localNode_ = 0;
    uint32_t mirrored_ = 0;
    uint32_t suppressed_ = 0;
    uint32_t injected_ = 0;
};

} // namespace meshcompromise
