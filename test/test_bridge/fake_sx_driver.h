#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "meshcompromise/host_radio.h"
#include "meshcompromise/sx_driver.h"

namespace meshcompromise
{

struct SentFrame {
    std::vector<uint8_t> bytes;
};

class FakeSxDriver : public SxDriver
{
  public:
    std::vector<std::string> ops;
    std::vector<SentFrame> sent;
    std::vector<LoraProfile> configured;

    bool cadDetected = false;
    bool inboundInProgress = false;
    bool irq = false;
    bool configureFails = false;
    bool primedOverride = true;
    int retunes = 0;
    int8_t lastTxPower = 0;
    bool transmitFails = false;
    CadResult cadSupport = CadResult::Free;

    uint8_t syncWord = 0;
    uint16_t preamble = 0;
    LoraProfile active;
    float rssi = -100.0f;
    float snr = 5.0f;

    std::vector<uint8_t> pendingRx;

    bool standby() override
    {
        ops.push_back("standby");
        return true;
    }

    bool setSyncWord(uint8_t value) override
    {
        ops.push_back("setSyncWord");
        syncWord = value;
        active.syncWord = value;
        return true;
    }

    bool setPreambleLength(uint16_t symbols) override
    {
        ops.push_back("setPreambleLength");
        preamble = symbols;
        active.preambleSymbols = symbols;
        return true;
    }

    bool configure(const LoraProfile &profile, int8_t txPowerDbm) override
    {
        ops.push_back("configure");
        lastTxPower = txPowerDbm;
        if (configureFails)
            return false;
        configured.push_back(profile);
        active = profile;
        syncWord = profile.syncWord;
        preamble = profile.preambleSymbols;
        primedOverride = true;
        return true;
    }

    bool retune(const LoraProfile &profile, int8_t txPowerDbm) override
    {
        ops.push_back("retune");
        retunes++;
        return configure(profile, txPowerDbm);
    }

    bool startReceive() override
    {
        ops.push_back("startReceive");
        return true;
    }

    CadResult scanChannel() override
    {
        ops.push_back("scanChannel");
        if (cadSupport == CadResult::Unsupported)
            return CadResult::Unsupported;
        return cadDetected ? CadResult::Detected : CadResult::Free;
    }

    bool irqFired() override { return irq; }

    bool packetInProgress() override { return inboundInProgress; }

    void clearIrq() override { irq = false; }

    size_t packetLength() override { return pendingRx.size(); }

    bool readPacket(uint8_t *bytes, size_t length) override
    {
        ops.push_back("readPacket");
        if (length > pendingRx.size())
            return false;
        std::memcpy(bytes, pendingRx.data(), length);
        pendingRx.clear();
        return true;
    }

    bool startTransmit(const uint8_t *bytes, size_t length) override
    {
        ops.push_back("startTransmit");
        if (transmitFails)
            return false;
        SentFrame frame;
        frame.bytes.assign(bytes, bytes + length);
        sent.push_back(frame);
        return true;
    }

    bool finishTransmit() override
    {
        ops.push_back("finishTransmit");
        return true;
    }

    void attachRxIrq() override { ops.push_back("attachRxIrq"); }

    void attachTxIrq() override { ops.push_back("attachTxIrq"); }

    void detachIrq() override { ops.push_back("detachIrq"); }

    float lastRssi() override { return rssi; }

    float lastSnr() override { return snr; }

    bool primed() const override { return primedOverride; }

    void deliver(const std::vector<uint8_t> &frame)
    {
        pendingRx = frame;
        irq = true;
    }

    bool didOp(const std::string &name) const
    {
        for (const std::string &op : ops) {
            if (op == name)
                return true;
        }
        return false;
    }

    size_t countOp(const std::string &name) const
    {
        size_t total = 0;
        for (const std::string &op : ops) {
            if (op == name)
                total++;
        }
        return total;
    }

    void clearOps() { ops.clear(); }

    uint32_t ownershipViolationsSeen() const { return violations_; }

    void noteViolation() { violations_++; }

  private:
    uint32_t violations_ = 0;
};

class FakeHostRadio : public HostRadio
{
  public:
    bool sending = false;
    bool receiving = false;
    bool pendingTx = false;
    int restores = 0;
    LoraProfile profile = meshtasticNarrowSlowProfile();

    void bind(FakeSxDriver &driver) { driver_ = &driver; }

    bool isSending() override { return sending; }
    bool isActivelyReceiving() override { return receiving; }
    bool txPending() override { return pendingTx; }

    void restore() override
    {
        restores++;
        if (driver_ != nullptr)
            driver_->active = profile;
    }

    LoraProfile currentProfile() override { return profile; }
    int noiseFloor() override { return -120; }

  private:
    FakeSxDriver *driver_ = nullptr;
};

} // namespace meshcompromise
