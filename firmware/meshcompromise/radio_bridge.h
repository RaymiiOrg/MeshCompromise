#pragma once

#include "meshcompromise/meshcore_radio.h"
#include "meshcompromise/radiolib_sx_driver.h"

namespace meshcompromise
{

struct RadioBridgeHardware {
    RadioLibSxDriver driver;
    MeshtasticHostRadio host;
};

class RadioBridge : private RadioBridgeHardware, public MeshcoreRadio
{
  public:
    RadioBridge() : MeshcoreRadio(driver, host) {}

    bool begin(const LoraProfile &meshcore);

    static bool txVeto();
};

extern RadioBridge *radioBridge;

} // namespace meshcompromise
