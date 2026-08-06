#pragma once

#include "mesh/MeshTypes.h"
#include "meshcompromise/mirror.h"

namespace meshcompromise
{

class TextInjector
{
  public:
    explicit TextInjector(Mirror &mirror) : mirror_(mirror) {}

    bool inject(uint32_t fromNode, uint32_t toNode, const char *text, size_t length);
    bool announce(const char *text, size_t length);

  private:
    meshtastic_MeshPacket *build(uint32_t fromNode, uint32_t toNode, const char *text, size_t length);
    bool dispatch(meshtastic_MeshPacket *packet);
    bool dispatchAsReceived(meshtastic_MeshPacket *packet);

    Mirror &mirror_;
};

} // namespace meshcompromise
