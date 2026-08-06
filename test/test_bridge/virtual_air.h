#pragma once

#include <cmath>
#include <vector>

#include "fake_sx_driver.h"
#include "meshcompromise/lora_profile.h"

namespace meshcompromise
{

struct AirFrame {
    LoraProfile profile;
    std::vector<uint8_t> bytes;
};

class VirtualAir
{
  public:
    std::vector<AirFrame> transmitted;
    uint32_t delivered = 0;
    uint32_t missed = 0;

    static bool hearable(const LoraProfile &sender, const LoraProfile &receiver)
    {
        if (sender.spreadingFactor != receiver.spreadingFactor)
            return false;
        if (std::fabs(sender.bandwidthKhz - receiver.bandwidthKhz) > 0.001f)
            return false;
        if (sender.syncWord != receiver.syncWord)
            return false;
        return frequenciesInterchangeable(sender.frequencyMhz, receiver.frequencyMhz, receiver.bandwidthKhz);
    }

    void transmit(const LoraProfile &profile, const std::vector<uint8_t> &bytes)
    {
        transmitted.push_back({profile, bytes});
    }

    bool deliverTo(FakeSxDriver &driver, const LoraProfile &sender, const std::vector<uint8_t> &bytes)
    {
        if (!hearable(sender, driver.active)) {
            missed++;
            return false;
        }
        driver.deliver(bytes);
        delivered++;
        return true;
    }

    void reset()
    {
        transmitted.clear();
        delivered = 0;
        missed = 0;
    }
};

} // namespace meshcompromise
