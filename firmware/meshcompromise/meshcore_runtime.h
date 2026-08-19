#pragma once

#include "helpers/ArduinoHelpers.h"
#include "helpers/IdentityStore.h"
#include "helpers/SimpleMeshTables.h"
#include "helpers/StaticPoolPacketManager.h"
#include "meshcompromise/meshcore_stack.h"
#include "meshcompromise/radio_bridge.h"

namespace meshcompromise
{

constexpr int kMeshcorePoolSize = 8;
constexpr const char *kMeshcoreIdentityName = "meshcompromise";

class MeshcoreRuntime
{
  public:
    MeshcoreRuntime();

    bool begin(mesh::Radio &radio);
    void loop();
    bool started() const { return started_; }
    MeshcoreStack *stack() { return stack_; }
    bool identityRestored() const { return identityRestored_; }

  private:
    ArduinoMillis clock_;
    StdRNG rng_;
    VolatileRTCClock rtc_;
    SimpleMeshTables tables_;
    StaticPoolPacketManager manager_;
    MeshcoreStack *stack_ = nullptr;
    bool started_ = false;
    bool identityRestored_ = false;
};

} // namespace meshcompromise
