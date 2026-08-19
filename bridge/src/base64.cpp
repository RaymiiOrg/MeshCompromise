#include "meshcompromise/base64.h"

#include <cstring>

namespace meshcompromise
{

namespace
{

int sextet(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

} // namespace

size_t decodeBase64(const char *text, uint8_t *out, size_t capacity)
{
    if (text == nullptr || out == nullptr || capacity == 0)
        return 0;

    uint32_t accumulator = 0;
    int bits = 0;
    size_t written = 0;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        const char c = *cursor;
        if (c == '=' || c == '\n' || c == '\r' || c == ' ')
            continue;

        const int value = sextet(c);
        if (value < 0)
            return 0;

        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            if (written >= capacity)
                return 0;
            out[written++] = static_cast<uint8_t>((accumulator >> bits) & 0xFF);
        }
    }

    return written;
}

} // namespace meshcompromise
