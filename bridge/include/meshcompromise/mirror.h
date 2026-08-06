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
    MirrorPolicy policy = MirrorPolicy::LocalOnly;
    uint8_t meshcoreChannel = 0;
};

struct MirrorMessage {
    uint8_t payload[kMeshcoreMaxPayload] = {0};
    size_t length = 0;
    uint8_t channel = 0;
    bool truncated = false;
};

size_t truncateUtf8(const char *text, size_t length, size_t limit);

bool buildMirrorMessage(const char *text, size_t length, uint8_t channel, MirrorMessage &out);

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
};

} // namespace meshcompromise
