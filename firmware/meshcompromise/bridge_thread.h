#pragma once

#include "concurrency/OSThread.h"
#include "meshcompromise/bridge_cycle.h"
#include "meshcompromise/meshcore_runtime.h"
#include "meshcompromise/radio_bridge.h"
#include "meshcompromise/settings.h"
#include "meshcompromise/slice_scheduler.h"

namespace meshcompromise
{

class BridgeThread : public concurrency::OSThread
{
  public:
    BridgeThread();

    void applySettings(const BridgeSettings &settings);
    const BridgeSettings &settings() const { return settings_; }
    const SliceStats &stats() const { return scheduler_.stats(); }
    SwitchMode mode() const { return scheduler_.mode(); }
    SliceState sliceState() const { return scheduler_.state(); }
    RadioBridge &radio() { return radio_; }
    MeshcoreRuntime &meshcore() { return meshcore_; }

  protected:
    int32_t runOnce() override;

  private:
    void refreshProfiles();
    void sendAdvert();
    void sendStats(uint32_t uptimeSeconds);
    void syncMeshcoreClock();
    void restoreContacts();
    void persistContacts();

    RadioBridge radio_;
    MeshcoreRuntime meshcore_;
    BridgeSettings settings_;
    SliceScheduler scheduler_;
    BridgeCycle cycle_;
    bool meshcoreClockSynced_ = false;
    uint8_t savedContactCount_ = 0;
};

extern BridgeThread *bridgeThread;

void setupBridge();

} // namespace meshcompromise
