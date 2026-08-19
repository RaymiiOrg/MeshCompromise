#pragma once

#include <Dispatcher.h>

#include "meshcompromise/radio_arbiter.h"

namespace meshcompromise
{

class MeshcoreRadio : public mesh::Radio
{
  public:
    MeshcoreRadio(SxDriver &driver, HostRadio &host);

    void setMeshcoreProfile(const LoraProfile &profile);
    void setTxPower(int8_t dbm);

    RadioArbiter &arbiter() { return arbiter_; }
    const RadioArbiter &arbiter() const { return arbiter_; }
    LoraProfile currentMeshtasticProfile() { return host_.currentProfile(); }

    void begin() override {}
    int recvRaw(uint8_t *bytes, int sz) override;
    uint32_t getEstAirtimeFor(int len_bytes) override;
    float packetScore(float snr, int packet_len) override;
    bool startSendRaw(const uint8_t *bytes, int len) override;
    bool isSendComplete() override;
    void onSendFinished() override;
    void loop() override {}
    int getNoiseFloor() const override;
    bool isInRecvMode() const override;
    bool isReceiving() override;
    float getLastRSSI() const override { return arbiter_.lastRssi(); }
    float getLastSNR() const override { return arbiter_.lastSnr(); }

    uint32_t packetsReceived() const { return arbiter_.received(); }
    uint32_t packetsSent() const { return arbiter_.sent(); }
    uint32_t txDropped() const { return arbiter_.txDropped(); }

  private:
    HostRadio &host_;
    RadioArbiter arbiter_;
};

} // namespace meshcompromise
