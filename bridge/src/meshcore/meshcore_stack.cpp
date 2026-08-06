#include "meshcompromise/meshcore_stack.h"

#include <cstdio>
#include <cstring>

#include "meshcompromise/base64.h"
#include "meshcompromise/bridge_log.h"

namespace meshcompromise
{

MeshcoreStack::MeshcoreStack(mesh::Radio &radio, mesh::MillisecondClock &clock, mesh::RNG &rng, mesh::RTCClock &rtc,
                             mesh::PacketManager &manager, mesh::MeshTables &tables)
    : mesh::Mesh(radio, clock, rng, rtc, manager, tables)
{
}

bool MeshcoreStack::addChannelFromPsk(const char *pskBase64)
{
    if (pskBase64 == nullptr || channelCount_ >= kMeshcoreMaxChannels)
        return false;

    mesh::GroupChannel &channel = channels_[channelCount_];
    memset(channel.secret, 0, sizeof(channel.secret));

    const size_t length = decodeBase64(pskBase64, channel.secret, sizeof(channel.secret));
    if (length != 16 && length != 32)
        return false;

    mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret, static_cast<int>(length));
    channelCount_++;
    return true;
}

bool MeshcoreStack::addChannelFromName(const char *name)
{
    if (name == nullptr || channelCount_ >= kMeshcoreMaxChannels)
        return false;

    mesh::GroupChannel &channel = channels_[channelCount_];
    memset(channel.secret, 0, sizeof(channel.secret));

    mesh::Utils::sha256(channel.secret, 16, reinterpret_cast<const uint8_t *>(name), static_cast<int>(strlen(name)));
    mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret, 16);
    channelCount_++;
    return true;
}

int MeshcoreStack::searchChannelsByHash(const uint8_t *hash, mesh::GroupChannel channels[], int max_matches)
{
    int found = 0;
    for (uint8_t i = 0; i < channelCount_ && found < max_matches; i++) {
        if (channels_[i].hash[0] == hash[0])
            channels[found++] = channels_[i];
    }
    return found;
}

bool MeshcoreStack::sendGroupText(uint8_t channelIndex, const char *senderName, const char *text, size_t length)
{
    if (channelIndex >= channelCount_)
        return false;

    GroupTextPayload payload;
    if (!buildGroupTextPayload(getRTCClock()->getCurrentTime(), senderName, text, length, payload))
        return false;

    mesh::Packet *packet =
        createGroupDatagram(kMeshcorePayloadGrpTxt, channels_[channelIndex], payload.bytes, payload.length);
    if (packet == nullptr)
        return false;

    sendFlood(packet);
    textsSent_++;
    return true;
}

bool MeshcoreStack::sendAdvert(const char *name)
{
    AdvertDataBuilder builder(ADV_TYPE_CHAT, name != nullptr && name[0] != '\0' ? name : kMeshcoreAdvertName);

    uint8_t appData[MAX_ADVERT_DATA_SIZE] = {0};
    const uint8_t appLength = builder.encodeTo(appData);

    mesh::Packet *packet = createAdvert(self_id, appData, appLength);
    if (packet == nullptr)
        return false;

    sendFlood(packet);
    advertsSent_++;
    return true;
}

size_t MeshcoreStack::exportContacts(MeshcoreContact *out, size_t max) const
{
    if (out == nullptr)
        return 0;

    size_t written = 0;
    for (int i = 0; i < kMeshcoreMaxContacts && written < max; i++) {
        if (contacts_[i].used)
            out[written++] = contacts_[i];
    }
    return written;
}

bool MeshcoreStack::importContact(const MeshcoreContact &contact)
{
    if (!contact.used || contact.nodeNum == 0)
        return false;

    for (int i = 0; i < kMeshcoreMaxContacts; i++) {
        if (contacts_[i].used && memcmp(contacts_[i].pubKey, contact.pubKey, PUB_KEY_SIZE) == 0)
            return true;
    }

    for (int i = 0; i < kMeshcoreMaxContacts; i++) {
        if (!contacts_[i].used) {
            contacts_[i] = contact;
            return true;
        }
    }
    return false;
}

const MeshcoreContact *MeshcoreStack::contactByName(const char *name) const
{
    if (name == nullptr || name[0] == '\0')
        return nullptr;

    for (int i = 0; i < kMeshcoreMaxContacts; i++) {
        if (contacts_[i].used && strncmp(contacts_[i].name, name, kMeshcoreNameLength) == 0)
            return &contacts_[i];
    }
    return nullptr;
}

const MeshcoreContact *MeshcoreStack::contactByNodeNum(uint32_t nodeNum) const
{
    for (int i = 0; i < kMeshcoreMaxContacts; i++) {
        if (contacts_[i].used && contacts_[i].nodeNum == nodeNum)
            return &contacts_[i];
    }
    return nullptr;
}

uint8_t MeshcoreStack::contactCount() const
{
    uint8_t count = 0;
    for (int i = 0; i < kMeshcoreMaxContacts; i++) {
        if (contacts_[i].used)
            count++;
    }
    return count;
}

void MeshcoreStack::learnContact(const mesh::Identity &id, const uint8_t *appData, size_t appLength)
{
    int slot = -1;
    for (int i = 0; i < kMeshcoreMaxContacts; i++) {
        if (contacts_[i].used && memcmp(contacts_[i].pubKey, id.pub_key, PUB_KEY_SIZE) == 0) {
            slot = i;
            break;
        }
        if (!contacts_[i].used && slot < 0)
            slot = i;
    }

    if (slot < 0)
        return;

    MeshcoreContact &contact = contacts_[slot];
    const bool isNew = !contact.used;

    memcpy(contact.pubKey, id.pub_key, PUB_KEY_SIZE);
    contact.nodeNum = nodeNumFromPubKey(id.pub_key);
    contact.used = true;

    char previousName[kMeshcoreNameLength] = {0};
    memcpy(previousName, contact.name, sizeof(previousName));

    AdvertDataParser parser(appData, static_cast<uint8_t>(appLength));
    if (parser.isValid() && parser.hasName()) {
        strncpy(contact.name, parser.getName(), kMeshcoreNameLength - 1);
        contact.name[kMeshcoreNameLength - 1] = 0;
    } else if (isNew) {
        snprintf(contact.name, kMeshcoreNameLength, "mc%02x%02x", id.pub_key[0], id.pub_key[1]);
    }

    const bool renamed = memcmp(previousName, contact.name, sizeof(previousName)) != 0;
    if (sink_ != nullptr && (isNew || renamed))
        sink_->onMeshcoreContact(contact);
}

int MeshcoreStack::searchPeersByHash(const uint8_t *hash)
{
    matchCount_ = 0;
    for (int i = 0; i < kMeshcoreMaxContacts && matchCount_ < kMeshcoreMaxContacts; i++) {
        if (contacts_[i].used && contacts_[i].pubKey[0] == hash[0])
            matches_[matchCount_++] = i;
    }

    if (matchCount_ == 0)
        MC_LOG_WARN("direct message from unknown MeshCore peer %02x, %u contact(s) known, need its advert first", hash[0],
                    static_cast<unsigned>(contactCount()));

    return matchCount_;
}

void MeshcoreStack::getPeerSharedSecret(uint8_t *secret, int peerIndex)
{
    if (peerIndex < 0 || peerIndex >= matchCount_)
        return;

    mesh::Identity peer;
    memcpy(peer.pub_key, contacts_[matches_[peerIndex]].pubKey, PUB_KEY_SIZE);
    self_id.calcSharedSecret(secret, peer);
}

void MeshcoreStack::onPeerDataRecv(mesh::Packet *packet, uint8_t type, int senderIndex, const uint8_t *secret,
                                   uint8_t *data, size_t len)
{
    if (type != kMeshcorePayloadTxtMsg || len <= kGroupTextHeader)
        return;
    if (senderIndex < 0 || senderIndex >= matchCount_)
        return;
    if ((data[4] >> 2) != 0)
        return;

    directsHeard_++;
    recordText(data, len);

    MeshcoreContact &from = contacts_[matches_[senderIndex]];
    sendDirectAck(packet, secret, from, data, len);

    if (sink_ == nullptr)
        return;

    char text[kMeshtasticMaxText + 1] = {0};
    const size_t extracted = extractGroupText(data, len, text, sizeof(text));
    if (extracted > 0)
        sink_->onMeshcoreDirectText(from, text, extracted);
}

void MeshcoreStack::sendDirectAck(mesh::Packet *packet, const uint8_t *secret, const MeshcoreContact &from,
                                  const uint8_t *data, size_t len)
{
    if (packet == nullptr || len <= kGroupTextHeader)
        return;

    const size_t textLength = strnlen(reinterpret_cast<const char *>(data) + kGroupTextHeader, len - kGroupTextHeader);

    uint8_t ackHash[6] = {0};
    mesh::Utils::sha256(ackHash, 4, data, static_cast<int>(kGroupTextHeader + textLength), from.pubKey, PUB_KEY_SIZE);
    if (kGroupTextHeader + textLength + 1 < len)
        ackHash[4] = data[kGroupTextHeader + textLength + 1];
    getRNG()->random(&ackHash[5], 1);

    mesh::Identity peer;
    memcpy(peer.pub_key, from.pubKey, PUB_KEY_SIZE);

    if (packet->isRouteFlood()) {
        mesh::Packet *path = createPathReturn(peer, secret, packet->path, packet->path_len, kMeshcorePayloadAck, ackHash,
                                              sizeof(ackHash));
        if (path != nullptr) {
            sendFlood(path, kMeshcoreAckDelayMs);
            directAcksSent_++;
        }
        return;
    }

    mesh::Packet *ack = createAck(ackHash, sizeof(ackHash));
    if (ack != nullptr) {
        sendFlood(ack, kMeshcoreAckDelayMs);
        directAcksSent_++;
    }
}

bool MeshcoreStack::sendDirectText(uint32_t nodeNum, const char *text, size_t length)
{
    const MeshcoreContact *contact = contactByNodeNum(nodeNum);
    if (contact == nullptr)
        return false;

    GroupTextPayload payload;
    if (!buildDirectTextPayload(getRTCClock()->getCurrentTime(), text, length, payload))
        return false;

    mesh::Identity peer;
    memcpy(peer.pub_key, contact->pubKey, PUB_KEY_SIZE);

    uint8_t secret[PUB_KEY_SIZE] = {0};
    self_id.calcSharedSecret(secret, peer);

    mesh::Packet *packet = createDatagram(kMeshcorePayloadTxtMsg, peer, secret, payload.bytes, payload.length);
    if (packet == nullptr)
        return false;

    sendFlood(packet);
    directsSent_++;
    return true;
}

bool MeshcoreStack::allowPacketForward(const mesh::Packet *packet)
{
    if (packet == nullptr)
        return false;
    if (packet->getPathHashCount() >= hopLimit_)
        return false;
    return mesh::Mesh::allowPacketForward(packet);
}

void MeshcoreStack::recordText(const uint8_t *data, size_t len)
{
    if (data == nullptr || len <= kGroupTextHeader)
        return;

    const uint8_t *text = data + kGroupTextHeader;
    const size_t textLength = len - kGroupTextHeader;
    const size_t copied = textLength < sizeof(lastText_) - 1 ? textLength : sizeof(lastText_) - 1;

    memcpy(lastText_, text, copied);
    lastText_[copied] = '\0';
}

void MeshcoreStack::onAdvertRecv(mesh::Packet *, const mesh::Identity &id, uint32_t, const uint8_t *appData,
                                 size_t appLength)
{
    advertsHeard_++;
    MC_LOG_INFO("MeshCore advert heard from %02x%02x", id.pub_key[0], id.pub_key[1]);
    learnContact(id, appData, appLength);
}

void MeshcoreStack::onGroupDataRecv(mesh::Packet *, uint8_t type, const mesh::GroupChannel &, uint8_t *data, size_t len)
{
    if (type != kMeshcorePayloadGrpTxt || len < kGroupTextHeader)
        return;
    if ((data[4] >> 2) != 0)
        return;

    textsHeard_++;
    recordText(data, len);

    if (sink_ != nullptr) {
        char text[kMeshtasticMaxText + 1] = {0};
        const size_t extracted = extractGroupText(data, len, text, sizeof(text));
        if (extracted > 0)
            sink_->onMeshcoreText(text, extracted);
    }
}

} // namespace meshcompromise
