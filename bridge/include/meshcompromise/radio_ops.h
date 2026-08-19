#pragma once

#include "meshcompromise/lora_profile.h"

namespace meshcompromise
{

class RadioOps
{
  public:
    virtual ~RadioOps() = default;

    virtual bool meshtasticBusy() = 0;

    virtual bool meshtasticTxPending() = 0;

    virtual void enterMeshcore(SwitchMode mode, const LoraProfile &profile) = 0;

    virtual void leaveMeshcore(SwitchMode mode, const LoraProfile &profile) = 0;

    virtual bool channelActive() = 0;

    virtual bool meshcoreReceiving() = 0;

    virtual bool meshcorePacketInProgress() = 0;

    virtual bool meshcoreTxPending() = 0;

    virtual bool meshcoreTxBusy() = 0;

    virtual void pumpMeshcore() = 0;
};

} // namespace meshcompromise
