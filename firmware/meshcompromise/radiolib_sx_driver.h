#pragma once

#include <RadioLib.h>

#include "mesh/RadioLibInterface.h"
#include "meshcompromise/host_radio.h"
#include "meshcompromise/sx1262_driver.h"

namespace meshcompromise
{

struct RadioLibSxHardware {
    RadioLibSxHardware();

    Module module;
    SX1262 radio;
};

class RadioLibSxDriver : private RadioLibSxHardware, public Sx1262Driver
{
  public:
    RadioLibSxDriver();

    bool begin();

  private:
    static void onIrq();

    static RadioLibSxDriver *instance_;
};

class MeshtasticHostRadio : public HostRadio
{
  public:
    bool isSending() override;
    bool isActivelyReceiving() override;
    bool txPending() override;
    void restore() override;
    LoraProfile currentProfile() override;
    int noiseFloor() override;
};

} // namespace meshcompromise
