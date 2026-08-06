#include "meshcompromise/radio_bridge.h"

extern bool (*meshCompromiseTxVeto)();

namespace meshcompromise
{

RadioBridge *radioBridge = nullptr;

bool RadioBridge::txVeto()
{
    return radioBridge != nullptr && radioBridge->arbiter().leaseHeld();
}

bool RadioBridge::begin(const LoraProfile &meshcore)
{
    setMeshcoreProfile(meshcore);
    const bool ready = driver.begin();

    radioBridge = this;
    meshCompromiseTxVeto = &RadioBridge::txVeto;

    return ready;
}

} // namespace meshcompromise
