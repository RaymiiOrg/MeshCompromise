#pragma once

#include <cstdint>

namespace meshcompromise
{

constexpr uint32_t kMinuteMs = 60000;

class PeriodicTimer
{
  public:
    void start(uint32_t now, uint32_t firstDelayMs);
    void rearm(uint32_t now, uint32_t delayMs);
    void setIntervalMinutes(uint16_t minutes) { intervalMinutes_ = minutes; }

    uint16_t intervalMinutes() const { return intervalMinutes_; }
    bool enabled() const { return intervalMinutes_ != 0; }
    bool armed() const { return armed_; }
    uint32_t nextDueMs() const { return nextMs_; }

    bool due(uint32_t now);

  private:
    uint16_t intervalMinutes_ = 0;
    uint32_t nextMs_ = 0;
    bool armed_ = false;
};

} // namespace meshcompromise
