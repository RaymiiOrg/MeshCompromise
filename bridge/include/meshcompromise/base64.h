#pragma once

#include <cstddef>
#include <cstdint>

namespace meshcompromise
{

size_t decodeBase64(const char *text, uint8_t *out, size_t capacity);

} // namespace meshcompromise
