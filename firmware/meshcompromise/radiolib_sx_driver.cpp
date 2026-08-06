#include "meshcompromise/radiolib_sx_driver.h"

#include <SPI.h>

#include "configuration.h"
#include "mesh/MeshRadio.h"
#include "mesh/NodeDB.h"

extern RadioLibHal *RadioLibHAL;

namespace meshcompromise
{

namespace
{
constexpr uint8_t kMeshtasticSyncWord = 0x2b;
} // namespace

RadioLibSxDriver *RadioLibSxDriver::instance_ = nullptr;

RadioLibSxHardware::RadioLibSxHardware()
    : module(static_cast<LockingArduinoHal *>(RadioLibHAL), LORA_CS, LORA_DIO1, LORA_RESET, SX126X_BUSY), radio(&module)
{
}

RadioLibSxDriver::RadioLibSxDriver() : Sx1262Driver(radio)
{
    instance_ = this;
    setIrqAction(&RadioLibSxDriver::onIrq);

    Sx1262Options options;
#ifdef SX126X_DIO2_AS_RF_SWITCH
    options.dio2AsRfSwitch = true;
#endif
    options.rxBoostedGain = config.lora.sx126x_rx_boosted_gain;
    setOptions(options);
}

void RadioLibSxDriver::onIrq()
{
    if (instance_ != nullptr)
        instance_->raiseIrq();
}

bool RadioLibSxDriver::begin()
{
    LOG_INFO("MeshCompromise driver bound cs=%d dio1=%d reset=%d busy=%d dio2rf=%d rxboost=%d", LORA_CS, LORA_DIO1,
             LORA_RESET, SX126X_BUSY, options().dio2AsRfSwitch ? 1 : 0, options().rxBoostedGain ? 1 : 0);
    return Sx1262Driver::begin();
}

bool MeshtasticHostRadio::isSending()
{
    RadioLibInterface *iface = RadioLibInterface::instance;
    return iface == nullptr || iface->isSending();
}

bool MeshtasticHostRadio::isActivelyReceiving()
{
    RadioLibInterface *iface = RadioLibInterface::instance;
    return iface != nullptr && iface->isActivelyReceiving();
}

bool MeshtasticHostRadio::txPending()
{
    RadioLibInterface *iface = RadioLibInterface::instance;
    return iface != nullptr && !iface->canSleep(false);
}

void MeshtasticHostRadio::restore()
{
    if (RadioLibInterface::instance != nullptr)
        RadioLibInterface::instance->reconfigure();
    else
        LOG_ERROR("MeshCompromise cannot restore the radio, no RadioLibInterface");
}

LoraProfile MeshtasticHostRadio::currentProfile()
{
    LoraProfile profile;
    profile.syncWord = kMeshtasticSyncWord;
    profile.preambleSymbols = 16;

    if (RadioLibInterface::instance != nullptr)
        profile.frequencyMhz = RadioLibInterface::instance->getFreq();

    float bwKHz = 250.0f;
    uint8_t sf = 11;
    uint8_t cr = 5;

    if (config.lora.use_preset) {
        modemPresetToParams(config.lora.modem_preset, false, bwKHz, sf, cr);
    } else {
        bwKHz = clampBandwidthKHz(bwCodeToKHz(static_cast<uint16_t>(config.lora.bandwidth)));
        sf = static_cast<uint8_t>(config.lora.spread_factor);
        cr = static_cast<uint8_t>(config.lora.coding_rate);
    }

    profile.bandwidthKhz = bwKHz;
    profile.spreadingFactor = sf;
    profile.codingRate = cr;
    return profile;
}

int MeshtasticHostRadio::noiseFloor()
{
    if (RadioLibInterface::instance != nullptr)
        return RadioLibInterface::instance->getNoiseFloor();
    return 0;
}

} // namespace meshcompromise
