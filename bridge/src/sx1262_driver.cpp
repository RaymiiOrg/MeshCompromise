#include "meshcompromise/sx1262_driver.h"

#include <Arduino.h>

#include "meshcompromise/bridge_log.h"

namespace meshcompromise
{

bool Sx1262Driver::ok(int state, const char *what)
{
    lastError_ = state;
    if (state == RADIOLIB_ERR_NONE)
        return true;
    MC_LOG_WARN("%s failed, RadioLib %d", what, state);
    return false;
}

bool Sx1262Driver::begin()
{
    return true;
}

bool Sx1262Driver::standby()
{
    return ok(radio_.standby(), "standby");
}

bool Sx1262Driver::setSyncWord(uint8_t syncWord)
{
    return ok(radio_.setSyncWord(syncWord), "setSyncWord");
}

bool Sx1262Driver::setPreambleLength(uint16_t symbols)
{
    return ok(radio_.setPreambleLength(symbols), "setPreambleLength");
}

void Sx1262Driver::reapplySensitivity()
{
    if (radio_.getMod()->SPIsetRegValue(kSx1262RxSensitivityReg, 0x01, 0, 0) != RADIOLIB_ERR_NONE)
        MC_LOG_WARN("could not re-apply the 0x8B5 rx sensitivity patch");
}

void Sx1262Driver::reapplyPostCalibrationSettings()
{
    ok(radio_.setDio2AsRfSwitch(options_.dio2AsRfSwitch), "setDio2AsRfSwitch");
    ok(radio_.setRxBoostedGainMode(options_.rxBoostedGain), "setRxBoostedGainMode");
    reapplySensitivity();
}

bool Sx1262Driver::configure(const LoraProfile &profile, int8_t txPowerDbm)
{
    const int state = radio_.begin(profile.frequencyMhz, profile.bandwidthKhz, profile.spreadingFactor, profile.codingRate,
                                   profile.syncWord, txPowerDbm, profile.preambleSymbols, kSx1262TcxoVoltage);
    if (!ok(state, "configure"))
        return false;

    radio_.setCurrentLimit(kSx1262CurrentLimitMa);
    radio_.setCRC(2);
    reapplyPostCalibrationSettings();

    primed_ = true;

    const uint32_t now = millis();
    if (!everLoggedConfigure_ || now - lastConfigureLogMs_ >= kSx1262ConfigureLogIntervalMs) {
        everLoggedConfigure_ = true;
        lastConfigureLogMs_ = now;
        MC_LOG_DEBUG("modem set to sf%u bw%u cr%u sync 0x%02x", static_cast<unsigned>(profile.spreadingFactor),
                     static_cast<unsigned>(profile.bandwidthKhz), static_cast<unsigned>(profile.codingRate),
                     static_cast<unsigned>(profile.syncWord));
    }
    return true;
}

bool Sx1262Driver::retune(const LoraProfile &profile, int8_t txPowerDbm)
{
    if (!primed_)
        return configure(profile, txPowerDbm);

    bool settled = true;
    settled &= ok(radio_.setFrequency(profile.frequencyMhz, true), "setFrequency");
    settled &= ok(radio_.setBandwidth(profile.bandwidthKhz), "setBandwidth");
    settled &= ok(radio_.setSpreadingFactor(profile.spreadingFactor), "setSpreadingFactor");
    settled &= ok(radio_.setCodingRate(profile.codingRate), "setCodingRate");
    settled &= ok(radio_.setSyncWord(profile.syncWord), "setSyncWord");
    settled &= ok(radio_.setPreambleLength(profile.preambleSymbols), "setPreambleLength");
    settled &= ok(radio_.setOutputPower(txPowerDbm), "setOutputPower");

    if (!settled)
        return configure(profile, txPowerDbm);

    reapplySensitivity();
    return true;
}

bool Sx1262Driver::startReceive()
{
    const uint32_t flags =
        RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED) | (1UL << RADIOLIB_IRQ_HEADER_VALID);
    return ok(radio_.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, flags, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0), "startReceive");
}

CadResult Sx1262Driver::scanChannel()
{
    const int state = radio_.scanChannel();
    if (state == RADIOLIB_LORA_DETECTED)
        return CadResult::Detected;
    if (state == RADIOLIB_CHANNEL_FREE)
        return CadResult::Free;
    return CadResult::Unsupported;
}

bool Sx1262Driver::packetInProgress()
{
    const uint32_t flags = radio_.getIrqFlags();
    return (flags & (RADIOLIB_SX126X_IRQ_HEADER_VALID | RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED)) != 0;
}

size_t Sx1262Driver::packetLength()
{
    return radio_.getPacketLength();
}

bool Sx1262Driver::readPacket(uint8_t *bytes, size_t length)
{
    return ok(radio_.readData(bytes, length), "readData");
}

bool Sx1262Driver::startTransmit(const uint8_t *bytes, size_t length)
{
    return ok(radio_.startTransmit(bytes, length), "startTransmit");
}

bool Sx1262Driver::finishTransmit()
{
    return ok(radio_.finishTransmit(), "finishTransmit");
}

void Sx1262Driver::attachRxIrq()
{
    radio_.setPacketReceivedAction(irqAction_);
}

void Sx1262Driver::attachTxIrq()
{
    radio_.setPacketSentAction(irqAction_);
}

void Sx1262Driver::detachIrq()
{
    radio_.clearPacketReceivedAction();
}

float Sx1262Driver::lastRssi()
{
    return radio_.getRSSI();
}

float Sx1262Driver::lastSnr()
{
    return radio_.getSNR();
}

} // namespace meshcompromise
