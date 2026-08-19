#include "meshcompromise/contact_id.h"

namespace meshcompromise
{

uint32_t nodeNumFromPubKey(const uint8_t *pubKey)
{
    if (pubKey == nullptr)
        return 0;

    uint32_t value = 0;
    for (int i = 0; i < 4; i++)
        value = (value << 8) | pubKey[i];

    if (value == 0 || value == 1 || value == 0xFFFFFFFFu)
        value ^= 0x5A5A5A5Au;
    return value;
}

uint32_t nodeNumFromMeshcoreName(const char *name)
{
    if (name == nullptr || name[0] == '\0')
        return kMeshcorePublicSenderNodeNum;

    uint32_t hash = 0x811C9DC5u;
    for (const char *c = name; *c != '\0'; c++) {
        hash ^= static_cast<uint8_t>(*c);
        hash *= 0x01000193u;
    }

    if (hash == 0 || hash == 1 || hash == 0xFFFFFFFFu || hash == kMeshcorePublicSenderNodeNum)
        hash ^= 0x5A5A5A5Au;
    return hash;
}

} // namespace meshcompromise
