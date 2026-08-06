#include "meshcompromise/mirror.h"

#include <cstring>

namespace meshcompromise
{

namespace
{
bool isUtf8Continuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}
} // namespace

size_t truncateUtf8(const char *text, size_t length, size_t limit)
{
    if (text == nullptr || length == 0 || limit == 0)
        return 0;
    if (length <= limit)
        return length;

    size_t cut = limit;
    while (cut > 0 && isUtf8Continuation(static_cast<unsigned char>(text[cut])))
        cut--;
    return cut;
}

bool buildMirrorMessage(const char *text, size_t length, uint8_t channel, MirrorMessage &out)
{
    out.length = 0;
    out.channel = channel;
    out.truncated = false;

    if (text == nullptr || length == 0)
        return false;

    const size_t usable = truncateUtf8(text, length, kMeshcoreMaxPayload);
    if (usable == 0)
        return false;

    std::memcpy(out.payload, text, usable);
    out.length = usable;
    out.truncated = usable < length;
    return true;
}

void MirrorHistory::record(uint32_t packetId)
{
    if (contains(packetId))
        return;
    entries_[next_] = packetId;
    next_ = (next_ + 1) % kMirrorHistorySize;
    if (count_ < kMirrorHistorySize)
        count_++;
}

bool MirrorHistory::contains(uint32_t packetId) const
{
    for (size_t i = 0; i < count_; i++) {
        if (entries_[i] == packetId)
            return true;
    }
    return false;
}

void MirrorHistory::clear()
{
    next_ = 0;
    count_ = 0;
}

Mirror::Mirror(const MirrorConfig &config) : config_(config) {}

void Mirror::setConfig(const MirrorConfig &config)
{
    config_ = config;
}

void Mirror::setLocalNode(uint32_t nodeNum)
{
    localNode_ = nodeNum;
}

void Mirror::resetCounters()
{
    mirrored_ = 0;
    suppressed_ = 0;
}

MirrorDecision Mirror::evaluate(const MirrorSource &source) const
{
    if (!config_.enabled)
        return MirrorDecision::NotEnabled;
    if (!source.isTextMessage)
        return MirrorDecision::NotText;
    if (!source.isBroadcast)
        return MirrorDecision::NotBroadcast;
    if (config_.policy == MirrorPolicy::LocalOnly && source.fromNode != localNode_)
        return MirrorDecision::NotLocal;
    if (history_.contains(source.packetId))
        return MirrorDecision::AlreadyMirrored;
    return MirrorDecision::Send;
}

MirrorDecision Mirror::prepare(const MirrorSource &source, const char *text, size_t length, MirrorMessage &out)
{
    const MirrorDecision decision = evaluate(source);
    if (decision != MirrorDecision::Send) {
        suppressed_++;
        return decision;
    }

    if (!buildMirrorMessage(text, length, config_.meshcoreChannel, out)) {
        suppressed_++;
        return MirrorDecision::EmptyPayload;
    }

    history_.record(source.packetId);
    mirrored_++;
    return MirrorDecision::Send;
}

} // namespace meshcompromise
