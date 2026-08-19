#pragma once

#include <RadioLib.h>

#include "meshcompromise/sx_driver.h"

namespace meshcompromise
{

constexpr float kSx1262TcxoVoltage = 1.8f;
constexpr float kSx1262CurrentLimitMa = 140.0f;
constexpr uint16_t kSx1262RxSensitivityReg = 0x8B5;
// In Split mode configure() runs on every Meshtastic<->MeshCore handback
// (tens of times a second); logging that every time is exactly the spam
// MeshCompromise's own log lines are supposed to avoid. First call always
// logs (so it's visible once per boot even if nothing changes again for a
// while), then at most once per interval after that.
constexpr uint32_t kSx1262ConfigureLogIntervalMs = 60000;

struct Sx1262Options {
    bool dio2AsRfSwitch = false;
    bool rxBoostedGain = false;
};

class Sx1262Driver : public SxDriver
{
  public:
    explicit Sx1262Driver(SX1262 &radio) : radio_(radio) {}

    void setOptions(const Sx1262Options &options) { options_ = options; }
    const Sx1262Options &options() const { return options_; }

    bool begin();
    bool standby() override;
    bool setSyncWord(uint8_t syncWord) override;
    bool setPreambleLength(uint16_t symbols) override;
    bool configure(const LoraProfile &profile, int8_t txPowerDbm) override;
    bool retune(const LoraProfile &profile, int8_t txPowerDbm) override;
    bool startReceive() override;
    CadResult scanChannel() override;
    bool irqFired() override { return irqFlag_; }
    bool packetInProgress() override;
    void clearIrq() override { irqFlag_ = false; }
    size_t packetLength() override;
    bool readPacket(uint8_t *bytes, size_t length) override;
    bool startTransmit(const uint8_t *bytes, size_t length) override;
    bool finishTransmit() override;
    void attachRxIrq() override;
    void attachTxIrq() override;
    void detachIrq() override;
    float lastRssi() override;
    float lastSnr() override;
    void reapplySensitivity() override;
    bool primed() const override { return primed_; }

    void setIrqAction(void (*action)(void)) { irqAction_ = action; }
    void raiseIrq() { irqFlag_ = true; }
    int lastError() const { return lastError_; }

  protected:
    SX1262 &radio_;

  private:
    bool ok(int state, const char *what);
    void reapplyPostCalibrationSettings();

    Sx1262Options options_;
    void (*irqAction_)(void) = nullptr;
    volatile bool irqFlag_ = false;
    int lastError_ = 0;
    bool primed_ = false;
    bool everLoggedConfigure_ = false;
    uint32_t lastConfigureLogMs_ = 0;
};

} // namespace meshcompromise
