#pragma once

#include "meshcompromise/lora_profile.h"

namespace meshcompromise
{

class HostRadio
{
  public:
    virtual ~HostRadio() = default;

    virtual bool isSending() = 0;

    virtual bool isActivelyReceiving() = 0;

    virtual bool txPending() = 0;

    virtual void restore() = 0;

    virtual LoraProfile currentProfile() = 0;

    virtual int noiseFloor() = 0;
};

} // namespace meshcompromise
