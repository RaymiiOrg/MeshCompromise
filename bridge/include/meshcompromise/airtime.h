#pragma once

#include <cstdint>

#include "meshcompromise/lora_profile.h"

namespace meshcompromise
{

float symbolTimeMs(float bandwidthKhz, uint8_t spreadingFactor);

bool lowDataRateOptimize(float bandwidthKhz, uint8_t spreadingFactor);

float preambleTimeMs(const LoraProfile &profile);

uint32_t payloadSymbolCount(const LoraProfile &profile, uint16_t payloadBytes);

float packetAirtimeMs(const LoraProfile &profile, uint16_t payloadBytes);

uint32_t recommendedScanPeriodMs(const LoraProfile &profile);

bool scanPeriodCoversPreamble(uint32_t periodMs, const LoraProfile &profile);

uint32_t maxScanPeriodMs(const LoraProfile &profile);

uint32_t minimumDetectDwellMs(const LoraProfile &profile);

uint32_t scanExcursionMs(const LoraProfile &meshcore, SwitchMode mode);

bool hostToleratesScanning(const LoraProfile &meshtastic, const LoraProfile &meshcore, SwitchMode mode);

float packetScore(float snr, uint8_t spreadingFactor, int packetLength);

} // namespace meshcompromise
