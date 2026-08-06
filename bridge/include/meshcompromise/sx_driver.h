#pragma once

#include <cstddef>
#include <cstdint>

#include "meshcompromise/lora_profile.h"

namespace meshcompromise
{

enum class CadResult { Free, Detected, Unsupported };

class SxDriver
{
  public:
    virtual ~SxDriver() = default;

    virtual bool standby() = 0;

    virtual bool setSyncWord(uint8_t syncWord) = 0;

    virtual bool setPreambleLength(uint16_t symbols) = 0;

    virtual bool configure(const LoraProfile &profile, int8_t txPowerDbm) = 0;

    virtual bool retune(const LoraProfile &profile, int8_t txPowerDbm) { return configure(profile, txPowerDbm); }

    virtual bool startReceive() = 0;

    virtual CadResult scanChannel() = 0;

    virtual bool irqFired() = 0;

    virtual bool packetInProgress() = 0;

    virtual void clearIrq() = 0;

    virtual size_t packetLength() = 0;

    virtual bool readPacket(uint8_t *bytes, size_t length) = 0;

    virtual bool startTransmit(const uint8_t *bytes, size_t length) = 0;

    virtual bool finishTransmit() = 0;

    virtual void attachRxIrq() = 0;

    virtual void attachTxIrq() = 0;

    virtual void detachIrq() = 0;

    virtual float lastRssi() = 0;

    virtual float lastSnr() = 0;

    virtual void reapplySensitivity() {}

    virtual bool primed() const { return true; }
};

} // namespace meshcompromise
