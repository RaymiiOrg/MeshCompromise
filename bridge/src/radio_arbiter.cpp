#include "meshcompromise/radio_arbiter.h"

#include "meshcompromise/bridge_log.h"

#include <algorithm>
#include <cstring>

namespace meshcompromise
{

RadioArbiter::RadioArbiter(SxDriver &driver, HostRadio &host) : driver_(driver), host_(host)
{
    meshcore_ = meshcoreDefaultProfile();
}

void RadioArbiter::setMeshcoreProfile(const LoraProfile &profile)
{
    meshcore_ = profile;
}

void RadioArbiter::setTxPower(int8_t dbm)
{
    txPowerDbm_ = static_cast<int8_t>(dbm < -9 ? -9 : (dbm > 22 ? 22 : dbm));
}

bool RadioArbiter::meshtasticBusy()
{
    return host_.isSending() || host_.isActivelyReceiving();
}

bool RadioArbiter::meshtasticTxPending()
{
    return host_.txPending();
}

void RadioArbiter::enterMeshcore(SwitchMode mode, const LoraProfile &profile)
{
    if (leaseHeld_)
        return;

    meshcore_ = profile;
    leaseHeld_ = true;

    driver_.standby();

    const bool bandUnchanged = sameImageCalibrationBand(host_.currentProfile().frequencyMhz, meshcore_.frequencyMhz);

    if (mode == SwitchMode::Aligned && driver_.primed()) {
        driver_.setSyncWord(meshcore_.syncWord);
        driver_.setPreambleLength(meshcore_.preambleSymbols);
    } else if (!(bandUnchanged ? driver_.retune(meshcore_, txPowerDbm_)
                               : driver_.configure(meshcore_, txPowerDbm_))) {
        MC_LOG_ERROR("arbiter could not reprogram the modem for MeshCore, handing back");
        leaseHeld_ = false;
        host_.restore();
        return;
    }

    if (!armReceive())
        MC_LOG_WARN("arbiter could not arm MeshCore receive");

    // No per-slice "took the radio" log here on purpose - this fires every
    // scan/dwell cycle (tens of times a second) and the mode/sync word almost
    // never change between calls. Actual traffic already logs itself in
    // drainRx()/serviceTx(); a mode change is logged once by refreshProfiles().
}

void RadioArbiter::leaveMeshcore(SwitchMode mode, const LoraProfile &meshtasticProfile)
{
    if (!leaseHeld_)
        return;

    driver_.detachIrq();
    driver_.standby();
    sending_ = false;

    // Mirror enterMeshcore(): in Aligned mode only the sync word and preamble
    // differ from MeshCore's settings, so a cheap direct restore is enough.
    // The old unconditional host_.restore() forced a full RadioLibInterface
    // reconfigure() - with its own multi-line INFO logging - on every single
    // handback (tens of times a second), which was most of the log spam and
    // also ate a lot of the CPU time other threads (the display) needed.
    // Split mode still needs the full restore since it changes real radio
    // parameters. profileRefreshIntervalMs governs how stale meshtasticProfile
    // can get if the user edits Meshtastic's own LoRa settings mid-session.
    if (mode == SwitchMode::Aligned && driver_.primed()) {
        driver_.setSyncWord(meshtasticProfile.syncWord);
        driver_.setPreambleLength(meshtasticProfile.preambleSymbols);
    } else {
        host_.restore();
    }

    driver_.reapplySensitivity();
    leaseHeld_ = false;
}

bool RadioArbiter::armReceive()
{
    driver_.clearIrq();
    driver_.attachRxIrq();
    return driver_.startReceive();
}

bool RadioArbiter::channelActive()
{
    if (!leaseHeld_)
        return false;
    if (driver_.irqFired())
        return true;

    // CAD leaves the modem in a one-shot scan state either way (detected or
    // free), so it never has a packet buffered - re-arm the real receiver
    // immediately in both cases. Doing this only on Free left a "Detected"
    // result short-circuited into drainRx() while the radio was still in the
    // CAD's own IRQ state, so the very frame CAD just found got read as an
    // empty buffer and dropped instead of being received.
    const CadResult result = driver_.scanChannel();
    armReceive();
    return result == CadResult::Detected;
}

void RadioArbiter::drainRx()
{
    if (!leaseHeld_ || rxReady_ || !driver_.irqFired())
        return;

    driver_.clearIrq();

    const size_t available = driver_.packetLength();
    if (available == 0 || available > kArbiterFrameSize) {
        MC_LOG_WARN("arbiter dropped a %u byte MeshCore frame", static_cast<unsigned>(available));
        armReceive();
        return;
    }

    if (driver_.readPacket(rx_.bytes, available)) {
        rx_.length = available;
        rxReady_ = true;
        lastRssi_ = driver_.lastRssi();
        lastSnr_ = driver_.lastSnr();
        received_++;
        MC_LOG_INFO("MeshCore rx %u bytes rssi=%d snr=%d", static_cast<unsigned>(available), static_cast<int>(lastRssi_),
                    static_cast<int>(lastSnr_));
    }

    armReceive();
}

void RadioArbiter::serviceTx()
{
    if (rxReady_ || txCount_ == 0)
        return;

    const ArbiterFrame &frame = txQueue_[txHead_];

    driver_.detachIrq();
    driver_.clearIrq();
    driver_.attachTxIrq();

    if (driver_.startTransmit(frame.bytes, frame.length)) {
        sending_ = true;
        MC_LOG_INFO("MeshCore tx %u bytes", static_cast<unsigned>(frame.length));
    } else {
        txDropped_++;
        MC_LOG_WARN("MeshCore tx of %u bytes failed to start", static_cast<unsigned>(frame.length));
    }

    txHead_ = (txHead_ + 1) % kArbiterTxDepth;
    txCount_--;
}

void RadioArbiter::pumpMeshcore()
{
    if (!leaseHeld_)
        return;

    if (sending_) {
        if (driver_.irqFired()) {
            driver_.clearIrq();
            driver_.finishTransmit();
            sending_ = false;
            sent_++;
            armReceive();
        }
        return;
    }

    drainRx();
    serviceTx();
}

bool RadioArbiter::meshcoreReceiving()
{
    return leaseHeld_ && !sending_ && (rxReady_ || driver_.irqFired());
}

bool RadioArbiter::meshcorePacketInProgress()
{
    return leaseHeld_ && !sending_ && driver_.packetInProgress();
}

bool RadioArbiter::meshcoreTxPending()
{
    return txCount_ > 0;
}

bool RadioArbiter::meshcoreTxBusy()
{
    return sending_;
}

bool RadioArbiter::txIdle() const
{
    return txCount_ == 0 && !sending_;
}

bool RadioArbiter::queueTx(const uint8_t *bytes, size_t length)
{
    if (bytes == nullptr || length == 0 || length > kArbiterFrameSize)
        return false;
    if (txCount_ >= kArbiterTxDepth) {
        txDropped_++;
        MC_LOG_WARN("MeshCore tx queue full, dropped %u bytes (%u dropped total)", static_cast<unsigned>(length),
                    static_cast<unsigned>(txDropped_));
        return false;
    }

    ArbiterFrame &slot = txQueue_[txTail_];
    std::memcpy(slot.bytes, bytes, length);
    slot.length = length;
    txTail_ = (txTail_ + 1) % kArbiterTxDepth;
    txCount_++;
    return true;
}

size_t RadioArbiter::takeRx(uint8_t *bytes, size_t capacity)
{
    if (!rxReady_ || bytes == nullptr || capacity == 0)
        return 0;

    const size_t copied = std::min(capacity, rx_.length);
    std::memcpy(bytes, rx_.bytes, copied);
    rxReady_ = false;
    rx_.length = 0;
    return copied;
}

} // namespace meshcompromise
