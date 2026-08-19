#pragma once

#include <Mesh.h>
#include <Utils.h>

#include <helpers/AdvertDataHelpers.h>

#include "meshcompromise/contact_id.h"
#include "meshcompromise/meshcore_sink.h"
#include "meshcompromise/mirror.h"

namespace meshcompromise
{

constexpr int kMeshcoreMaxChannels = 4;
constexpr int kMeshcoreMaxContacts = 16;
constexpr const char *kMeshcorePublicPsk = "izOH6cXN6mrJ5e26oRXNcg==";
constexpr const char *kMeshcoreAdvertName = "MeshCompromise";
constexpr uint8_t kMeshcorePayloadAck = 0x03;
constexpr uint32_t kMeshcoreAckDelayMs = 200;

static_assert(kMeshcorePubKeySize == PUB_KEY_SIZE, "MeshCore public key size drifted");

class MeshcoreStack : public mesh::Mesh, public MeshcoreSink
{
  public:
    MeshcoreStack(mesh::Radio &radio, mesh::MillisecondClock &clock, mesh::RNG &rng, mesh::RTCClock &rtc,
                  mesh::PacketManager &manager, mesh::MeshTables &tables);

    void setTextSink(GroupTextSink *sink) { sink_ = sink; }

    bool addChannelFromPsk(const char *pskBase64);
    bool addChannelFromName(const char *name);
    bool hasChannel(uint8_t index) const { return index < channelCount_; }
    uint8_t channelCount() const override { return channelCount_; }

    void setHopLimit(uint8_t hops) { hopLimit_ = hops; }
    uint8_t hopLimit() const { return hopLimit_; }

    bool sendGroupText(uint8_t channelIndex, const char *senderName, const char *text, size_t length) override;
    bool sendAdvert(const char *name);

    bool sendDirectText(uint32_t nodeNum, const char *text, size_t length) override;
    const MeshcoreContact *contactByNodeNum(uint32_t nodeNum) const override;
    const MeshcoreContact *contactByName(const char *name) const override;
    uint8_t contactCount() const;
    size_t exportContacts(MeshcoreContact *out, size_t max) const;
    bool importContact(const MeshcoreContact &contact);
    uint32_t directsHeard() const { return directsHeard_; }
    uint32_t directsSent() const { return directsSent_; }
    uint32_t directAcksSent() const { return directAcksSent_; }

    uint32_t textsHeard() const { return textsHeard_; }
    uint32_t advertsHeard() const { return advertsHeard_; }
    uint32_t textsSent() const { return textsSent_; }
    uint32_t advertsSent() const { return advertsSent_; }
    const char *lastText() const { return lastText_; }

  protected:
    void onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp, const uint8_t *app_data,
                      size_t app_data_len) override;
    void onGroupDataRecv(mesh::Packet *packet, uint8_t type, const mesh::GroupChannel &channel, uint8_t *data,
                         size_t len) override;
    int searchChannelsByHash(const uint8_t *hash, mesh::GroupChannel channels[], int max_matches) override;
    int searchPeersByHash(const uint8_t *hash) override;
    void getPeerSharedSecret(uint8_t *secret, int peerIndex) override;
    void onPeerDataRecv(mesh::Packet *packet, uint8_t type, int senderIndex, const uint8_t *secret, uint8_t *data,
                        size_t len) override;
    bool allowPacketForward(const mesh::Packet *packet) override;

  private:
    void recordText(const uint8_t *data, size_t len);
    void learnContact(const mesh::Identity &id, const uint8_t *appData, size_t appLength);
    void sendDirectAck(mesh::Packet *packet, const uint8_t *secret, const MeshcoreContact &from, const uint8_t *data,
                       size_t len);

    mesh::GroupChannel channels_[kMeshcoreMaxChannels];
    uint8_t channelCount_ = 0;
    uint8_t hopLimit_ = 3;
    GroupTextSink *sink_ = nullptr;
    uint32_t advertsSent_ = 0;
    uint32_t textsHeard_ = 0;
    uint32_t advertsHeard_ = 0;
    uint32_t textsSent_ = 0;
    char lastText_[64] = {0};
    MeshcoreContact contacts_[kMeshcoreMaxContacts];
    int matches_[kMeshcoreMaxContacts] = {0};
    int matchCount_ = 0;
    uint32_t directsHeard_ = 0;
    uint32_t directsSent_ = 0;
    uint32_t directAcksSent_ = 0;
};

} // namespace meshcompromise
