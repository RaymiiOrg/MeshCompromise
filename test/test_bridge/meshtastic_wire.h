#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <mesh/CryptoEngine.h>
#include <meshtastic/mesh.pb.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include "generated/meshtastic_wire_constants.h"

namespace meshcompromise
{

struct MeshtasticHeader {
    uint32_t to = 0;
    uint32_t from = 0;
    uint32_t id = 0;
    uint8_t flags = 0;
    uint8_t channel = 0;
    uint8_t nextHop = 0;
    uint8_t relayNode = 0;
};

inline uint8_t xorHash(const uint8_t *bytes, size_t length)
{
    uint8_t code = 0;
    for (size_t i = 0; i < length; i++)
        code ^= bytes[i];
    return code;
}

inline uint8_t meshtasticChannelHash(const char *name, const uint8_t *psk, size_t pskLength)
{
    return static_cast<uint8_t>(xorHash(reinterpret_cast<const uint8_t *>(name), std::strlen(name)) ^
                                xorHash(psk, pskLength));
}

inline uint8_t primaryChannelHash()
{
    return meshtasticChannelHash(kMeshtasticPrimaryChannelName, kMeshtasticDefaultPsk, sizeof(kMeshtasticDefaultPsk));
}

inline CryptoKey primaryChannelKey()
{
    CryptoKey key;
    std::memset(key.bytes, 0, sizeof(key.bytes));
    std::memcpy(key.bytes, kMeshtasticDefaultPsk, sizeof(kMeshtasticDefaultPsk));
    key.length = static_cast<int8_t>(sizeof(kMeshtasticDefaultPsk));
    return key;
}

inline void writeHeader(uint8_t *out, const MeshtasticHeader &header)
{
    std::memcpy(out + 0, &header.to, 4);
    std::memcpy(out + 4, &header.from, 4);
    std::memcpy(out + 8, &header.id, 4);
    out[12] = header.flags;
    out[13] = header.channel;
    out[14] = header.nextHop;
    out[15] = header.relayNode;
}

inline MeshtasticHeader readHeader(const uint8_t *bytes)
{
    MeshtasticHeader header;
    std::memcpy(&header.to, bytes + 0, 4);
    std::memcpy(&header.from, bytes + 4, 4);
    std::memcpy(&header.id, bytes + 8, 4);
    header.flags = bytes[12];
    header.channel = bytes[13];
    header.nextHop = bytes[14];
    header.relayNode = bytes[15];
    return header;
}

inline std::vector<uint8_t> encodeMeshtasticFrame(const meshtastic_MeshPacket &packet, const CryptoKey &key,
                                                  uint8_t channelHash)
{
    uint8_t plain[kMeshtasticMaxLoraPayload] = {0};
    pb_ostream_t stream = pb_ostream_from_buffer(plain, sizeof(plain));
    if (!pb_encode(&stream, &meshtastic_Data_msg, &packet.decoded))
        return {};

    const size_t length = stream.bytes_written;
    if (length + kMeshtasticHeaderLength > kMeshtasticMaxLoraPayload)
        return {};

    crypto->setKey(key);
    crypto->encryptPacket(packet.from, packet.id, length, plain);

    MeshtasticHeader header;
    header.to = packet.to;
    header.from = packet.from;
    header.id = packet.id;
    header.channel = channelHash;
    header.flags = static_cast<uint8_t>(packet.hop_limit & kFlagsHopLimitMask);
    header.flags |= static_cast<uint8_t>((packet.hop_start << kFlagsHopStartShift) & kFlagsHopStartMask);
    if (packet.want_ack)
        header.flags |= kFlagsWantAckMask;

    std::vector<uint8_t> frame(kMeshtasticHeaderLength + length, 0);
    writeHeader(frame.data(), header);
    std::memcpy(frame.data() + kMeshtasticHeaderLength, plain, length);
    return frame;
}

inline bool decodeMeshtasticFrame(const std::vector<uint8_t> &frame, const CryptoKey &key, MeshtasticHeader &header,
                                  meshtastic_Data &data)
{
    if (frame.size() <= kMeshtasticHeaderLength)
        return false;

    header = readHeader(frame.data());

    const size_t length = frame.size() - kMeshtasticHeaderLength;
    uint8_t plain[kMeshtasticMaxLoraPayload] = {0};
    std::memcpy(plain, frame.data() + kMeshtasticHeaderLength, length);

    crypto->setKey(key);
    crypto->decrypt(header.from, header.id, length, plain);

    data = meshtastic_Data_init_default;
    pb_istream_t stream = pb_istream_from_buffer(plain, length);
    return pb_decode(&stream, &meshtastic_Data_msg, &data);
}

inline std::string dataText(const meshtastic_Data &data)
{
    return std::string(reinterpret_cast<const char *>(data.payload.bytes), data.payload.size);
}

} // namespace meshcompromise
