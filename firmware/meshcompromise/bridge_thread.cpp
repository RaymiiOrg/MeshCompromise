#include "meshcompromise/bridge_thread.h"

#include <cstdio>

#include "NodeDB.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "mesh/MeshService.h"
#include "mesh/Router.h"
#include "meshcompromise/bridge_log.h"
#include "meshcompromise/bridge_ui.h"
#include "meshcompromise/contact_store.h"
#include "modules/NodeInfoModule.h"
#include "meshcompromise/mirror_module.h"
#include "meshcompromise/settings_store.h"
#include "meshcompromise/stats_text.h"

namespace meshcompromise
{

namespace
{
constexpr int32_t kIdleIntervalMs = 250;

const char *modeName(SwitchMode mode)
{
    return mode == SwitchMode::Aligned ? "aligned" : "split";
}

void forwardBridgeLog(BridgeLogLevel level, const char *message)
{
    switch (level) {
    case BridgeLogLevel::Error:
        LOG_ERROR("MeshCompromise %s", message);
        return;
    case BridgeLogLevel::Warn:
        LOG_WARN("MeshCompromise %s", message);
        return;
    case BridgeLogLevel::Info:
        LOG_INFO("MeshCompromise %s", message);
        return;
    default:
        LOG_DEBUG("MeshCompromise %s", message);
        return;
    }
}
} // namespace

BridgeThread *bridgeThread = nullptr;

BridgeThread::BridgeThread()
    : concurrency::OSThread("MeshCompromise"), settings_(defaultSettings()), scheduler_(radio_.arbiter(), settings_.slice)
{
    if (loadSettings(settings_))
        LOG_INFO("MeshCompromise settings restored from flash");
    else
        LOG_INFO("MeshCompromise using default settings");

    LOG_INFO("MeshCompromise MeshCore PHY %.4fMHz sf%u bw%.1fkHz cr%u sync 0x%02x tx %ddBm hops %u",
             settings_.meshcore.frequencyMhz, static_cast<unsigned>(settings_.meshcore.spreadingFactor),
             settings_.meshcore.bandwidthKhz, static_cast<unsigned>(settings_.meshcore.codingRate),
             static_cast<unsigned>(settings_.meshcore.syncWord), static_cast<int>(settings_.txPowerDbm),
             static_cast<unsigned>(settings_.hopLimit));

    radio_.setTxPower(settings_.txPowerDbm);
    if (!radio_.begin(settings_.meshcore))
        LOG_ERROR("MeshCompromise radio bridge failed to start");

    if (!meshcore_.begin(radio_))
        LOG_ERROR("MeshCompromise MeshCore stack failed to start");
    else
        LOG_INFO("MeshCompromise MeshCore stack up, identity %s, %u channel(s)",
                 meshcore_.identityRestored() ? "restored" : "generated",
                 meshcore_.stack() != nullptr ? static_cast<unsigned>(meshcore_.stack()->channelCount()) : 0u);

    if (meshcore_.stack() != nullptr) {
        meshcore_.stack()->setHopLimit(settings_.hopLimit);
        restoreContacts();
    }

    cycle_.setIntervals(0, settings_.advertIntervalMinutes, settings_.statsIntervalMinutes);
    refreshProfiles();
}

void BridgeThread::applySettings(const BridgeSettings &settings)
{
    if (!validateSettings(settings)) {
        LOG_WARN("MeshCompromise rejected invalid settings");
        return;
    }

    settings_ = settings;
    normalizeSettings(settings_);

    cycle_.setIntervals(millis(), settings_.advertIntervalMinutes, settings_.statsIntervalMinutes);

    if (!saveSettings(settings_))
        LOG_WARN("MeshCompromise could not persist settings");

    radio_.setMeshcoreProfile(settings_.meshcore);
    radio_.setTxPower(settings_.txPowerDbm);
    if (meshcore_.stack() != nullptr)
        meshcore_.stack()->setHopLimit(settings_.hopLimit);
    scheduler_.setConfig(settings_.slice);
    if (mirrorModule != nullptr)
        mirrorModule->setConfig(settings_.mirror);
    refreshProfiles();

    LOG_INFO("MeshCompromise settings applied, meshcore=%d mode=%s advert=%umin stats=%umin",
             settings_.meshcoreEnabled ? 1 : 0, modeName(scheduler_.mode()),
             static_cast<unsigned>(settings_.advertIntervalMinutes), static_cast<unsigned>(settings_.statsIntervalMinutes));
}

void BridgeThread::refreshProfiles()
{
    const SwitchMode before = scheduler_.mode();
    scheduler_.setProfiles(radio_.currentMeshtasticProfile(), settings_.meshcore);
    if (scheduler_.mode() != before)
        LOG_INFO("MeshCompromise switch mode is now %s", modeName(scheduler_.mode()));
}

void BridgeThread::restoreContacts()
{
    MeshcoreContact restored[kContactStoreCapacity];
    const size_t count = loadContacts(restored, kContactStoreCapacity);

    for (size_t i = 0; i < count; i++) {
        if (meshcore_.stack()->importContact(restored[i]))
            LOG_INFO("MeshCompromise restored MeshCore contact %s as 0x%x", restored[i].name, restored[i].nodeNum);
    }

    savedContactCount_ = meshcore_.stack()->contactCount();
}

void BridgeThread::persistContacts()
{
    MeshcoreStack *stack = meshcore_.stack();
    if (stack == nullptr)
        return;

    const uint8_t current = stack->contactCount();
    if (current == savedContactCount_)
        return;

    MeshcoreContact snapshot[kContactStoreCapacity];
    const size_t count = stack->exportContacts(snapshot, kContactStoreCapacity);

    if (saveContacts(snapshot, count)) {
        savedContactCount_ = current;
        LOG_INFO("MeshCompromise persisted %u MeshCore contact(s)", static_cast<unsigned>(count));
    }
}

void BridgeThread::syncMeshcoreClock()
{
    if (meshcoreClockSynced_)
        return;

    const uint32_t validTime = getValidTime(RTCQualityDevice);
    if (validTime == 0)
        return;

    MeshcoreStack *stack = meshcore_.stack();
    if (stack == nullptr || stack->getRTCClock() == nullptr)
        return;

    stack->getRTCClock()->setCurrentTime(validTime);
    meshcoreClockSynced_ = true;
    LOG_INFO("MeshCompromise synced MeshCore clock to %u", static_cast<unsigned>(validTime));
}

void BridgeThread::sendAdvert()
{
    MeshcoreStack *stack = meshcore_.stack();
    if (stack == nullptr || stack->channelCount() == 0)
        return;

    const char *name = owner.long_name[0] != '\0' ? owner.long_name : "MeshCompromise";
    if (stack->sendAdvert(name))
        LOG_INFO("MeshCompromise advertised as %s, %u sent so far", name, static_cast<unsigned>(stack->advertsSent()));
    else
        LOG_WARN("MeshCompromise could not advertise");

    if (nodeInfoModule != nullptr)
        nodeInfoModule->sendOurNodeInfo();
}

void BridgeThread::sendStats(uint32_t uptimeSeconds)
{
    StatsSnapshot snapshot;
    snapshot.uptimeSeconds = uptimeSeconds;
    snapshot.meshcoreHeard = radio_.packetsReceived();
    snapshot.meshcoreSent = radio_.packetsSent();
    snapshot.meshcoreDutyCycle = scheduler_.stats().meshcoreDutyCycle();
    snapshot.freeHeapBytes = ESP.getFreeHeap();
    snapshot.holdMs = scheduler_.holdMs();
    snapshot.switchOverheadMs = scheduler_.switchOverheadMs();

    if (mirrorModule != nullptr) {
        snapshot.mirroredOut = mirrorModule->mirror().mirroredCount();
        snapshot.mirroredIn = mirrorModule->injectedCount();
    }

    MeshcoreStack *stack = meshcore_.stack();
    if (stack != nullptr)
        snapshot.adverts = stack->advertsSent();

    char text[kStatsTextLength] = {0};
    const size_t length = buildStatsText(snapshot, text, sizeof(text));
    if (length == 0) {
        LOG_WARN("MeshCompromise could not build a statistics line");
        return;
    }

    LOG_INFO("MeshCompromise stats: %s", text);

    if (stack != nullptr && stack->channelCount() > 0) {
        char sender[24];
        snprintf(sender, sizeof(sender), "%s@MT", owner.short_name[0] != '\0' ? owner.short_name : "mtastic");
        if (!stack->sendGroupText(settings_.mirror.meshcoreChannel, sender, text, length))
            LOG_WARN("MeshCompromise could not send statistics on MeshCore");
    }

    if (mirrorModule != nullptr)
        mirrorModule->announce(text, length);
}

int32_t BridgeThread::runOnce()
{
    if (!settings_.meshcoreEnabled)
        return kIdleIntervalMs;

    const uint32_t now = millis();
    const CycleActions actions = cycle_.tick(now);

    if (actions.firstTick)
        LOG_INFO("MeshCompromise active, mode=%s", modeName(scheduler_.mode()));
    if (!meshcoreClockSynced_)
        syncMeshcoreClock();
    persistContacts();
    if (actions.refreshProfiles)
        refreshProfiles();
    if (actions.advertDue)
        sendAdvert();
    if (actions.statsDue)
        sendStats(actions.uptimeSeconds);

    // No per-slice state-transition log here on purpose - handleScan()/handleDwell()
    // run tens of times a second and almost every tick is an empty scan with
    // nothing to report. Actual traffic already logs itself in
    // RadioArbiter::drainRx()/serviceTx(); this trace added noise without signal.
    const uint32_t delay = scheduler_.tick(now);

    meshcore_.loop();
    return static_cast<int32_t>(BridgeCycle::clampDelay(delay));
}

void setupMirrorModule()
{
    if (mirrorModule == nullptr)
        mirrorModule = new MirrorModule();

    if (bridgeThread == nullptr)
        return;

    mirrorModule->setConfig(bridgeThread->settings().mirror);

    if (nodeDB != nullptr) {
        mirrorModule->setLocalNode(nodeDB->getNodeNum());
        LOG_INFO("MeshCompromise mirroring as node 0x%x", nodeDB->getNodeNum());
    } else {
        LOG_ERROR("MeshCompromise has no NodeDB, local-only mirroring will match nothing");
    }

    MeshcoreStack *stack = bridgeThread->meshcore().stack();
    mirrorModule->setSink(stack);
    if (stack != nullptr)
        stack->setTextSink(mirrorModule);
    else
        LOG_ERROR("MeshCompromise has no MeshCore stack, mirroring is dead in both directions");

    meshCompromiseOutboundHook = &MirrorModule::outboundHook;
    LOG_INFO("MeshCompromise outbound hook installed");
}

void setupBridge()
{
    if (bridgeThread != nullptr)
        return;

    bridgeLog = &forwardBridgeLog;
    LOG_INFO("MeshCompromise starting");
    bridgeThread = new BridgeThread();
    setupMirrorModule();
    setupBridgeUi();
    LOG_INFO("MeshCompromise ready");
}

} // namespace meshcompromise
