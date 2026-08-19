#pragma once

#include <cstddef>
#include <cstdint>

#include <meshtastic/mesh.pb.h>

namespace meshcompromise
{

bool buildInjectedText(meshtastic_MeshPacket &packet, uint32_t destination, uint8_t channel, const char *text,
                       size_t length);

} // namespace meshcompromise
