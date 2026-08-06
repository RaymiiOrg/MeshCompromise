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

uint8_t meshcoreHeaderByte(uint8_t routeType, uint8_t payloadType)
{
    return static_cast<uint8_t>((routeType & 0x03) | ((payloadType & 0x0F) << kMeshcoreTypeShift));
}

bool buildGroupTextPayload(uint32_t timestamp, const char *senderName, const char *text, size_t length,
                           GroupTextPayload &out)
{
    out.length = 0;
    out.truncated = false;

    if (senderName == nullptr || text == nullptr || length == 0)
        return false;

    const size_t nameLength = std::strlen(senderName);
    if (nameLength == 0 || nameLength + 2 >= kMeshcoreMaxText)
        return false;

    const size_t prefixLength = nameLength + 2;
    const size_t budget = kMeshcoreMaxText - prefixLength;
    const size_t usable = truncateUtf8(text, length, budget);
    if (usable == 0)
        return false;

    size_t index = 0;
    out.bytes[index++] = static_cast<uint8_t>(timestamp & 0xFF);
    out.bytes[index++] = static_cast<uint8_t>((timestamp >> 8) & 0xFF);
    out.bytes[index++] = static_cast<uint8_t>((timestamp >> 16) & 0xFF);
    out.bytes[index++] = static_cast<uint8_t>((timestamp >> 24) & 0xFF);
    out.bytes[index++] = kGroupTextPlain;

    std::memcpy(&out.bytes[index], senderName, nameLength);
    index += nameLength;
    out.bytes[index++] = ':';
    out.bytes[index++] = ' ';

    std::memcpy(&out.bytes[index], text, usable);
    index += usable;
    out.bytes[index] = 0;

    out.length = index;
    out.truncated = usable < length;
    return true;
}

bool buildDirectTextPayload(uint32_t timestamp, const char *text, size_t length, GroupTextPayload &out)
{
    out.length = 0;
    out.truncated = false;

    if (text == nullptr || length == 0)
        return false;

    const size_t usable = truncateUtf8(text, length, kMeshcoreMaxText);
    if (usable == 0)
        return false;

    size_t index = 0;
    out.bytes[index++] = static_cast<uint8_t>(timestamp & 0xFF);
    out.bytes[index++] = static_cast<uint8_t>((timestamp >> 8) & 0xFF);
    out.bytes[index++] = static_cast<uint8_t>((timestamp >> 16) & 0xFF);
    out.bytes[index++] = static_cast<uint8_t>((timestamp >> 24) & 0xFF);
    out.bytes[index++] = kGroupTextPlain;

    std::memcpy(&out.bytes[index], text, usable);
    index += usable;
    out.bytes[index] = 0;

    out.length = index;
    out.truncated = usable < length;
    return true;
}

size_t extractGroupText(const uint8_t *payload, size_t length, char *out, size_t capacity)
{
    if (payload == nullptr || out == nullptr || capacity == 0)
        return 0;
    if (length <= kGroupTextHeader)
        return 0;
    if ((payload[4] >> 2) != 0)
        return 0;

    const char *text = reinterpret_cast<const char *>(payload + kGroupTextHeader);
    size_t available = length - kGroupTextHeader;

    for (size_t i = 0; i < available; i++) {
        if (text[i] == '\0') {
            available = i;
            break;
        }
    }

    if (available == 0)
        return 0;

    const size_t limit = capacity - 1 < kMeshtasticMaxText ? capacity - 1 : kMeshtasticMaxText;
    const size_t usable = truncateUtf8(text, available, limit);
    if (usable == 0)
        return 0;

    std::memcpy(out, text, usable);
    out[usable] = '\0';
    return usable;
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
    injected_ = 0;
}

void Mirror::noteInjected(uint32_t packetId)
{
    history_.record(packetId);
    injected_++;
}

void Mirror::suppress(uint32_t packetId)
{
    history_.record(packetId);
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
