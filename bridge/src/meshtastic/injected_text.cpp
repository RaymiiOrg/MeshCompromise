#include "meshcompromise/injected_text.h"

#include <cstring>

#include "meshcompromise/mirror.h"

namespace meshcompromise
{

bool buildInjectedText(meshtastic_MeshPacket &packet, uint32_t destination, uint8_t channel, const char *text,
                       size_t length)
{
    if (text == nullptr || length == 0)
        return false;

    const size_t capacity = sizeof(packet.decoded.payload.bytes);
    const size_t usable = truncateUtf8(text, length, capacity);
    if (usable == 0)
        return false;

    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.to = destination;
    packet.channel = channel;
    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    std::memcpy(packet.decoded.payload.bytes, text, usable);
    packet.decoded.payload.size = static_cast<pb_size_t>(usable);
    return true;
}

} // namespace meshcompromise
