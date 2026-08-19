#include "meshcompromise/periodic.h"

namespace meshcompromise
{

void PeriodicTimer::start(uint32_t now, uint32_t firstDelayMs)
{
    nextMs_ = now + firstDelayMs;
    armed_ = true;
}

void PeriodicTimer::rearm(uint32_t now, uint32_t delayMs)
{
    start(now, delayMs);
}

bool PeriodicTimer::due(uint32_t now)
{
    if (!enabled() || !armed_)
        return false;
    if (static_cast<int32_t>(now - nextMs_) < 0)
        return false;

    nextMs_ = now + static_cast<uint32_t>(intervalMinutes_) * kMinuteMs;
    return true;
}

} // namespace meshcompromise
