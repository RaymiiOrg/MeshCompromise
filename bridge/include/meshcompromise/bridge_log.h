#pragma once

namespace meshcompromise
{

enum class BridgeLogLevel { Debug, Info, Warn, Error };

using BridgeLogFn = void (*)(BridgeLogLevel level, const char *message);

extern BridgeLogFn bridgeLog;

void bridgeLogf(BridgeLogLevel level, const char *format, ...);

} // namespace meshcompromise

#define MC_LOG_DEBUG(...) ::meshcompromise::bridgeLogf(::meshcompromise::BridgeLogLevel::Debug, __VA_ARGS__)
#define MC_LOG_INFO(...) ::meshcompromise::bridgeLogf(::meshcompromise::BridgeLogLevel::Info, __VA_ARGS__)
#define MC_LOG_WARN(...) ::meshcompromise::bridgeLogf(::meshcompromise::BridgeLogLevel::Warn, __VA_ARGS__)
#define MC_LOG_ERROR(...) ::meshcompromise::bridgeLogf(::meshcompromise::BridgeLogLevel::Error, __VA_ARGS__)
