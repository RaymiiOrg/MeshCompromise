#pragma once

#include <cstddef>
#include <cstdint>

namespace meshcompromise
{

constexpr size_t kMeshtasticHeaderLength = 16;
constexpr size_t kMeshtasticMaxLoraPayload = 255;
constexpr uint8_t kFlagsHopLimitMask = 0x07;
constexpr uint8_t kFlagsWantAckMask = 0x08;
constexpr uint8_t kFlagsViaMqttMask = 0x10;
constexpr uint8_t kFlagsHopStartMask = 0xE0;
constexpr uint8_t kFlagsHopStartShift = 5;

constexpr char kMeshtasticPrimaryChannelName[] = "LongFast";

constexpr uint8_t kMeshtasticDefaultPsk[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

} // namespace meshcompromise
