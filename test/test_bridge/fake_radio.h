#pragma once

#include <vector>

#include "meshcompromise/radio_ops.h"

namespace meshcompromise
{

class FakeRadio : public RadioOps
{
  public:
    bool busy = false;
    bool hostTxPending = false;
    bool cadActive = false;
    bool receiving = false;
    bool packetInProgress = false;
    bool txPending = false;
    bool txBusy = false;

    bool meshcoreOwnsRadio = false;
    int enterCount = 0;
    int leaveCount = 0;
    int pumpCount = 0;
    int ownershipViolations = 0;
    int busyDuringEnter = 0;
    std::vector<SwitchMode> enterModes;

    bool meshtasticBusy() override { return busy; }

    bool meshtasticTxPending() override { return hostTxPending; }

    void enterMeshcore(SwitchMode mode, const LoraProfile &) override
    {
        if (meshcoreOwnsRadio)
            ownershipViolations++;
        if (busy)
            busyDuringEnter++;
        meshcoreOwnsRadio = true;
        enterCount++;
        enterModes.push_back(mode);
    }

    void leaveMeshcore(SwitchMode, const LoraProfile &) override
    {
        if (!meshcoreOwnsRadio)
            ownershipViolations++;
        meshcoreOwnsRadio = false;
        leaveCount++;
    }

    bool channelActive() override { return cadActive; }

    bool meshcoreReceiving() override { return receiving; }

    bool meshcorePacketInProgress() override { return packetInProgress; }

    bool meshcoreTxPending() override { return txPending; }

    bool meshcoreTxBusy() override { return txBusy; }

    void pumpMeshcore() override
    {
        if (!meshcoreOwnsRadio)
            ownershipViolations++;
        pumpCount++;
    }

    void quiet()
    {
        cadActive = false;
        receiving = false;
        txPending = false;
        txBusy = false;
    }
};

} // namespace meshcompromise
