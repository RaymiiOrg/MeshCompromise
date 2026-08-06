#pragma once

#include "mesh/MeshModule.h"
#include "meshcompromise/contact_directory.h"
#include "meshcompromise/meshcore_sink.h"
#include "meshcompromise/mirror.h"
#include "meshcompromise/text_injector.h"

namespace meshcompromise
{

constexpr size_t kMeshcoreAnnouncedNodes = 8;
constexpr uint32_t kMeshcoreNodeInfoIntervalMs = 1800000;

class MirrorModule : public MeshModule, public GroupTextSink
{
  public:
    MirrorModule();

    void setConfig(const MirrorConfig &config);
    void setLocalNode(uint32_t nodeNum) { mirror_.setLocalNode(nodeNum); }
    const Mirror &mirror() const { return mirror_; }

    void setSink(MeshcoreSink *sink) { sink_ = sink; }
    MeshcoreSink *sink() const { return sink_; }

    void onMeshcoreText(const char *text, size_t length) override;
    void onMeshcoreDirectText(const MeshcoreContact &from, const char *text, size_t length) override;
    void onMeshcoreContact(const MeshcoreContact &contact) override;

    bool handleOutbound(meshtastic_MeshPacket *packet);
    static bool outboundHook(meshtastic_MeshPacket *packet);
    uint32_t directsBridgedCount() const { return directsBridged_; }
    void suppressMirror(uint32_t packetId) { mirror_.suppress(packetId); }
    uint32_t injectedCount() const { return mirror_.injectedCount(); }
    uint32_t contactsLearned() const { return contacts_.count(); }
    bool announce(const char *text, size_t length) { return injector_.announce(text, length); }

    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    bool claimDirect(meshtastic_MeshPacket *packet);
    void bridgeReceivedDirect(const meshtastic_MeshPacket &packet);
    void ackOnBehalfOfMeshcore(const meshtastic_MeshPacket &packet);
    void mirrorBroadcast(const meshtastic_MeshPacket &packet);
    void announceMeshcoreNode(uint32_t nodeNum, const char *longName, const char *shortName);

    struct AnnouncedNode {
        uint32_t nodeNum = 0;
        uint32_t lastAnnouncedMs = 0;
        bool used = false;
    };

    Mirror mirror_;
    TextInjector injector_{mirror_};
    ContactDirectory contacts_;
    MeshcoreSink *sink_ = nullptr;
    uint32_t directsBridged_ = 0;
    AnnouncedNode announced_[kMeshcoreAnnouncedNodes];
};

extern MirrorModule *mirrorModule;

void setupMirrorModule();

} // namespace meshcompromise
