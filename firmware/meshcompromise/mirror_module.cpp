#include "meshcompromise/mirror_module.h"

#include <cstdio>
#include <cstring>

#include "NodeDB.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "mesh/MeshService.h"
#include "mesh/Router.h"
#include "mesh/mesh-pb-constants.h"
#include "modules/RoutingModule.h"
#include "meshcompromise/contact_id.h"

namespace meshcompromise
{

namespace
{
constexpr size_t kMaxMeshcoreSenderName = 16;
}

MirrorModule *mirrorModule = nullptr;

MirrorModule::MirrorModule() : MeshModule("MeshCompromiseMirror"), mirror_(MirrorConfig())
{
    isPromiscuous = true;
    loopbackOk = false;
    if (nodeDB != nullptr)
        mirror_.setLocalNode(nodeDB->getNodeNum());
    LOG_INFO("MeshCompromise mirror module up, promiscuous=1 loopback=0 local=0x%x",
             nodeDB != nullptr ? nodeDB->getNodeNum() : 0);
}

void MirrorModule::setConfig(const MirrorConfig &config)
{
    mirror_.setConfig(config);
    LOG_INFO("MeshCompromise mirror config fwd=%d rev=%d channel=%u", config.enabled ? 1 : 0,
             config.reverseEnabled ? 1 : 0, static_cast<unsigned>(config.meshcoreChannel));
}

bool MirrorModule::wantPacket(const meshtastic_MeshPacket *p)
{
    if (p == nullptr)
        return false;
    if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    return p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP;
}

ProcessMessage MirrorModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (isBroadcast(mp.to))
        mirrorBroadcast(mp);
    else
        bridgeReceivedDirect(mp);
    return ProcessMessage::CONTINUE;
}

void MirrorModule::ackOnBehalfOfMeshcore(const meshtastic_MeshPacket &mp)
{
    if (!mp.want_ack || routingModule == nullptr)
        return;

    routingModule->sendAckNak(meshtastic_Routing_Error_NONE, getFrom(&mp), mp.id, mp.channel, 0);
    LOG_DEBUG("MeshCompromise acked id=0x%x on behalf of MeshCore 0x%x", mp.id, mp.to);
}

void MirrorModule::bridgeReceivedDirect(const meshtastic_MeshPacket &mp)
{
    if (!mirror_.config().enabled || sink_ == nullptr)
        return;
    if (isToUs(&mp))
        return;
    if (sink_->contactByNodeNum(mp.to) == nullptr)
        return;

    const char *text = reinterpret_cast<const char *>(mp.decoded.payload.bytes);
    const size_t length = mp.decoded.payload.size;
    if (length == 0)
        return;

    if (sink_->sendDirectText(mp.to, text, length)) {
        directsBridged_++;
        LOG_INFO("MeshCompromise relayed a Meshtastic DM from 0x%x to MeshCore 0x%x, %u bytes", getFrom(&mp), mp.to,
                 static_cast<unsigned>(length));
        ackOnBehalfOfMeshcore(mp);
    } else {
        LOG_WARN("MeshCompromise could not relay a Meshtastic DM to MeshCore 0x%x", mp.to);
    }
}

void MirrorModule::mirrorBroadcast(const meshtastic_MeshPacket &mp)
{
    MirrorSource source;
    source.packetId = mp.id;
    // A phone/on-device compose leaves mp.from at the 0 sentinel until
    // Router::send() normalizes it (well after our outboundHook runs for a
    // local send - see meshCompromiseOutboundHook in Router::sendLocal()).
    // Reading mp.from raw here made MirrorPolicy::LocalOnly's fromNode ==
    // localNode_ check compare 0 against our real node number and reject
    // our own outgoing broadcasts as "not local". getFrom() is the same
    // 0-means-us normalization the rest of Meshtastic already uses.
    source.fromNode = getFrom(&mp);
    source.toNode = mp.to;
    source.channel = mp.channel;
    source.isTextMessage = mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
                           mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP;
    source.isBroadcast = isBroadcast(mp.to);

    MirrorMessage message;
    const char *text = reinterpret_cast<const char *>(mp.decoded.payload.bytes);
    const size_t length = mp.decoded.payload.size;

    const MirrorDecision decision = mirror_.prepare(source, text, length, message);

    LOG_DEBUG("MeshCompromise mirror saw id=0x%x from=0x%x to=0x%x ch=%u len=%u decision=%d", mp.id, mp.from, mp.to,
              static_cast<unsigned>(mp.channel), static_cast<unsigned>(length), static_cast<int>(decision));

    if (decision != MirrorDecision::Send)
        return;

    if (sink_ == nullptr) {
        LOG_WARN("MeshCompromise has no MeshCore sink, dropping mirror of id=0x%x", mp.id);
        return;
    }

    char senderBuf[24];
    char fallbackBuf[16];
    const char *base;
    if (nodeDB != nullptr && source.fromNode == nodeDB->getNodeNum()) {
        base = owner.short_name[0] != '\0' ? owner.short_name : "mtastic";
    } else {
        meshtastic_NodeInfoLite *node = nodeDB != nullptr ? nodeDB->getMeshNode(source.fromNode) : nullptr;
        if (node != nullptr && nodeInfoLiteHasUser(node) && node->short_name[0] != '\0') {
            base = node->short_name;
        } else {
            snprintf(fallbackBuf, sizeof(fallbackBuf), "!%08x", source.fromNode);
            base = fallbackBuf;
        }
    }
    snprintf(senderBuf, sizeof(senderBuf), "%s@MT", base);
    const char *sender = senderBuf;

    if (!sink_->sendGroupText(mirror_.config().meshcoreChannel, sender, reinterpret_cast<const char *>(message.payload),
                              message.length))
        LOG_WARN("MeshCompromise could not mirror id=0x%x", mp.id);
    else
        LOG_INFO("MeshCompromise mirrored id=0x%x len=%u as \"%s\"", mp.id, static_cast<unsigned>(message.length),
                 reinterpret_cast<const char *>(message.payload));
}

void MirrorModule::announceMeshcoreNode(uint32_t nodeNum, const char *longName, const char *shortName)
{
    if (router == nullptr || service == nullptr || nodeNum == 0)
        return;

    const uint32_t now = millis();
    AnnouncedNode *slot = nullptr;
    AnnouncedNode *oldest = &announced_[0];

    for (size_t i = 0; i < kMeshcoreAnnouncedNodes; i++) {
        if (announced_[i].used && announced_[i].nodeNum == nodeNum) {
            if (now - announced_[i].lastAnnouncedMs < kMeshcoreNodeInfoIntervalMs)
                return;
            slot = &announced_[i];
            break;
        }
        if (!announced_[i].used) {
            slot = &announced_[i];
            break;
        }
        if (announced_[i].lastAnnouncedMs < oldest->lastAnnouncedMs)
            oldest = &announced_[i];
    }

    if (slot == nullptr)
        slot = oldest;

    meshtastic_MeshPacket *packet = router->allocForSending();
    if (packet == nullptr) {
        LOG_WARN("MeshCompromise could not allocate a node advert for 0x%x", nodeNum);
        return;
    }

    meshtastic_User user = meshtastic_User_init_default;
    snprintf(user.id, sizeof(user.id), "!%08x", nodeNum);
    strncpy(user.long_name, longName, sizeof(user.long_name) - 1);
    user.long_name[sizeof(user.long_name) - 1] = 0;
    strncpy(user.short_name, shortName, sizeof(user.short_name) - 1);
    user.short_name[sizeof(user.short_name) - 1] = 0;
    user.hw_model = meshtastic_HardwareModel_PRIVATE_HW;
    user.public_key.size = 0;

    packet->to = NODENUM_BROADCAST;
    packet->channel = 0;
    packet->decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
    packet->decoded.want_response = false;
    packet->decoded.payload.size = static_cast<pb_size_t>(pb_encode_to_bytes(
        packet->decoded.payload.bytes, sizeof(packet->decoded.payload.bytes), &meshtastic_User_msg, &user));
    if (packet->decoded.payload.size == 0) {
        LOG_WARN("MeshCompromise could not encode a node advert for 0x%x", nodeNum);
        packetPool.release(packet);
        return;
    }

    packet->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    packet->hop_start = packet->hop_limit;
    packet->from = nodeNum;

    slot->nodeNum = nodeNum;
    slot->lastAnnouncedMs = now;
    slot->used = true;

    LOG_INFO("MeshCompromise advertising MeshCore node 0x%x as %s", nodeNum, longName);
    service->sendToMesh(packet, RX_SRC_LOCAL, false);
}

void MirrorModule::onMeshcoreText(const char *text, size_t length)
{
    if (!mirror_.config().reverseEnabled) {
        LOG_DEBUG("MeshCompromise reverse mirror off, dropping MeshCore group text");
        return;
    }
    if (text == nullptr || length == 0)
        return;

    char senderName[32] = {0};
    char longBuf[40] = {0};
    const char *body = text;
    size_t bodyLength = length;
    uint32_t fromNode = kMeshcorePublicSenderNodeNum;
    const char *displayName = "MeshCore Public";
    const char *shortName = "MC";
    char shortBuf[5] = {0};

    const char *colon = static_cast<const char *>(memchr(text, ':', length));
    if (colon != nullptr) {
        const size_t nameLength = static_cast<size_t>(colon - text);
        bool nameOk = nameLength > 0 && nameLength <= kMaxMeshcoreSenderName;
        for (size_t i = 0; nameOk && i < nameLength; i++) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c <= 0x20 || c >= 0x7F)
                nameOk = false;
        }

        const char *candidateBody = colon + 1;
        size_t candidateLength = length - nameLength - 1;
        while (candidateLength > 0 && *candidateBody == ' ') {
            candidateBody++;
            candidateLength--;
        }

        if (nameOk && candidateLength > 0) {
            memcpy(senderName, text, nameLength);
            senderName[nameLength] = 0;
            body = candidateBody;
            bodyLength = candidateLength;
            const MeshcoreContact *known = sink_ != nullptr ? sink_->contactByName(senderName) : nullptr;
            fromNode = known != nullptr ? known->nodeNum : nodeNumFromMeshcoreName(senderName);
            snprintf(longBuf, sizeof(longBuf), "%s (MeshCore)", senderName);
            displayName = longBuf;
            strncpy(shortBuf, senderName, sizeof(shortBuf) - 1);
            shortName = shortBuf;
        }
    }

    if (nodeDB != nullptr) {
        meshtastic_NodeInfoLite *node = nodeDB->getOrCreateMeshNode(fromNode);
        if (node != nullptr) {
            node->public_key.size = 0;
            strncpy(node->long_name, displayName, sizeof(node->long_name) - 1);
            node->long_name[sizeof(node->long_name) - 1] = 0;
            strncpy(node->short_name, shortName, sizeof(node->short_name) - 1);
            node->short_name[sizeof(node->short_name) - 1] = 0;
            node->hw_model = meshtastic_HardwareModel_PRIVATE_HW;
            node->last_heard = getValidTime(RTCQualityFromNet);
            nodeInfoLiteSetBit(node, NODEINFO_BITFIELD_HAS_USER_MASK, true);
        }
    }

    announceMeshcoreNode(fromNode, displayName, shortName);
    injector_.inject(fromNode, NODENUM_BROADCAST, body, bodyLength);
}

void MirrorModule::onMeshcoreDirectText(const MeshcoreContact &from, const char *text, size_t length)
{
    if (!mirror_.config().reverseEnabled) {
        LOG_DEBUG("MeshCompromise reverse mirror off, dropping MeshCore direct text");
        return;
    }
    if (text == nullptr || length == 0)
        return;
    if (nodeDB == nullptr)
        return;

    directsBridged_++;
    LOG_INFO("MeshCompromise received MeshCore DM from %s (0x%x), %u bytes", from.name, from.nodeNum,
             static_cast<unsigned>(length));
    injector_.inject(from.nodeNum, nodeDB->getNodeNum(), text, length);
}

void MirrorModule::onMeshcoreContact(const MeshcoreContact &contact)
{
    contacts_.registerContact(contact);

    char longName[40] = {0};
    char shortName[5] = {0};
    snprintf(longName, sizeof(longName), "%s (MeshCore)", contact.name);
    strncpy(shortName, contact.name, sizeof(shortName) - 1);
    announceMeshcoreNode(contact.nodeNum, longName, shortName);
}

bool MirrorModule::handleOutbound(meshtastic_MeshPacket *packet)
{
    if (packet == nullptr || sink_ == nullptr)
        return false;
    if (packet->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    if (packet->decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP)
        return false;

    if (isBroadcast(packet->to)) {
        mirrorBroadcast(*packet);
        return false;
    }

    return claimDirect(packet);
}

bool MirrorModule::claimDirect(meshtastic_MeshPacket *packet)
{
    if (isToUs(packet))
        return false;

    if (sink_->contactByNodeNum(packet->to) == nullptr) {
        LOG_DEBUG("MeshCompromise leaves DM to 0x%x on Meshtastic, not a MeshCore contact", packet->to);
        return false;
    }

    const char *text = reinterpret_cast<const char *>(packet->decoded.payload.bytes);
    const size_t length = packet->decoded.payload.size;

    if (sink_->sendDirectText(packet->to, text, length)) {
        directsBridged_++;
        LOG_INFO("MeshCompromise sent MeshCore direct message to 0x%x, %u bytes", packet->to,
                 static_cast<unsigned>(length));
        ackOnBehalfOfMeshcore(*packet);
    } else {
        LOG_WARN("MeshCompromise could not send a MeshCore direct message to 0x%x", packet->to);
    }

    packetPool.release(packet);
    return true;
}

bool MirrorModule::outboundHook(meshtastic_MeshPacket *packet)
{
    return mirrorModule != nullptr && mirrorModule->handleOutbound(packet);
}

} // namespace meshcompromise
