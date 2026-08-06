#pragma once

#include <cstdint>

namespace meshcompromise
{

uint32_t nodeNumFromPubKey(const uint8_t *pubKey);

uint32_t nodeNumFromMeshcoreName(const char *name);

constexpr uint32_t kMeshcorePublicSenderNodeNum = 0x4D435253;

} // namespace meshcompromise
