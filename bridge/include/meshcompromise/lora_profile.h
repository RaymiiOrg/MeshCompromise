#pragma once

#include <cstdint>

namespace meshcompromise
{

struct LoraProfile {
    float frequencyMhz = 0.0f;
    float bandwidthKhz = 0.0f;
    uint8_t spreadingFactor = 0;
    uint8_t codingRate = 5;
    uint8_t syncWord = 0;
    uint16_t preambleSymbols = 0;
};

enum class SwitchMode { Aligned, Split };

bool operator==(const LoraProfile &a, const LoraProfile &b);
bool operator!=(const LoraProfile &a, const LoraProfile &b);

float frequencyToleranceKhz(float bandwidthKhz);
bool frequenciesInterchangeable(float aMhz, float bMhz, float bandwidthKhz);

SwitchMode selectSwitchMode(const LoraProfile &meshtastic, const LoraProfile &meshcore);

uint16_t meshcorePreambleSymbols(uint8_t spreadingFactor);

LoraProfile meshcoreDefaultProfile();
LoraProfile meshtasticNarrowSlowProfile();

} // namespace meshcompromise
