#pragma once

#include <cstddef>
#include <cstdint>

#include "meshcompromise/host_radio.h"
#include "meshcompromise/radio_ops.h"
#include "meshcompromise/sx_driver.h"

namespace meshcompromise
{

constexpr size_t kArbiterFrameSize = 256;
constexpr size_t kArbiterTxDepth = 8;

struct ArbiterFrame {
    uint8_t bytes[kArbiterFrameSize] = {0};
    size_t length = 0;
};

class RadioArbiter : public RadioOps
{
  public:
    RadioArbiter(SxDriver &driver, HostRadio &host);

    void setMeshcoreProfile(const LoraProfile &profile);
    const LoraProfile &meshcoreProfile() const { return meshcore_; }
    void setTxPower(int8_t dbm);

    bool leaseHeld() const { return leaseHeld_; }

    bool meshtasticBusy() override;
    bool meshtasticTxPending() override;
    void enterMeshcore(SwitchMode mode, const LoraProfile &profile) override;
    void leaveMeshcore(SwitchMode mode, const LoraProfile &profile) override;
    bool channelActive() override;
    bool meshcoreReceiving() override;
    bool meshcorePacketInProgress() override;
    bool meshcoreTxPending() override;
    bool meshcoreTxBusy() override;
    void pumpMeshcore() override;

    bool queueTx(const uint8_t *bytes, size_t length);
    size_t takeRx(uint8_t *bytes, size_t capacity);
    bool rxAvailable() const { return rxReady_; }
    bool txIdle() const;

    float lastRssi() const { return lastRssi_; }
    float lastSnr() const { return lastSnr_; }
    uint32_t received() const { return received_; }
    uint32_t sent() const { return sent_; }
    uint32_t txDropped() const { return txDropped_; }

  private:
    bool armReceive();
    void drainRx();
    void serviceTx();

    SxDriver &driver_;
    HostRadio &host_;
    LoraProfile meshcore_;
    int8_t txPowerDbm_ = 20;

    bool leaseHeld_ = false;
    bool sending_ = false;

    ArbiterFrame txQueue_[kArbiterTxDepth];
    size_t txHead_ = 0;
    size_t txTail_ = 0;
    size_t txCount_ = 0;

    ArbiterFrame rx_;
    bool rxReady_ = false;

    float lastRssi_ = 0.0f;
    float lastSnr_ = 0.0f;
    uint32_t received_ = 0;
    uint32_t sent_ = 0;
    uint32_t txDropped_ = 0;
};

} // namespace meshcompromise
