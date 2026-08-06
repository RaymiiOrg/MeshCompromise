#include "meshcompromise/meshcore_runtime.h"

#include <esp_random.h>

#include "FSCommon.h"
#include "configuration.h"

namespace meshcompromise
{

namespace
{
constexpr const char *kIdentityDir = "/meshcompromise";
}

MeshcoreRuntime::MeshcoreRuntime() : manager_(kMeshcorePoolSize) {}

bool MeshcoreRuntime::begin(mesh::Radio &radio)
{
    if (started_)
        return true;

    rng_.begin(static_cast<long>(esp_random()));

    stack_ = new MeshcoreStack(radio, clock_, rng_, rtc_, manager_, tables_);

    IdentityStore store(FSCom, kIdentityDir);
    store.begin();

    if (store.load(kMeshcoreIdentityName, stack_->self_id)) {
        identityRestored_ = true;
    } else {
        stack_->self_id = mesh::LocalIdentity(&rng_);
        if (!store.save(kMeshcoreIdentityName, stack_->self_id))
            LOG_WARN("MeshCompromise could not persist the MeshCore identity");
    }

    if (!stack_->addChannelFromPsk(kMeshcorePublicPsk))
        LOG_ERROR("MeshCompromise could not derive the MeshCore public channel");
    if (!stack_->addChannelFromName("#test"))
        LOG_WARN("MeshCompromise could not derive the MeshCore #test channel");

    stack_->begin();
    started_ = true;

    LOG_INFO("MeshCore stack started, identity %s, %u channel(s)", identityRestored_ ? "restored" : "generated",
             static_cast<unsigned>(stack_->channelCount()));
    return true;
}

void MeshcoreRuntime::loop()
{
    if (started_ && stack_ != nullptr)
        stack_->loop();
}

} // namespace meshcompromise
