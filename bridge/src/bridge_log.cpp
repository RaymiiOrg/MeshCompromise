#include "meshcompromise/bridge_log.h"

#include <cstdarg>
#include <cstdio>

namespace meshcompromise
{

namespace
{
constexpr size_t kLogBufferSize = 192;
}

BridgeLogFn bridgeLog = nullptr;

void bridgeLogf(BridgeLogLevel level, const char *format, ...)
{
    if (bridgeLog == nullptr || format == nullptr)
        return;

    char message[kLogBufferSize];

    va_list args;
    va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (written < 0)
        return;

    bridgeLog(level, message);
}

} // namespace meshcompromise
