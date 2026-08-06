#pragma once

#include "meshcompromise/meshcore_sink.h"

namespace meshcompromise
{

constexpr uint32_t kContactStoreMagic = 0x4D435031;
constexpr uint16_t kContactStoreVersion = 1;
constexpr size_t kContactStoreCapacity = 16;

struct StoredContacts {
    uint32_t magic = kContactStoreMagic;
    uint16_t version = kContactStoreVersion;
    uint16_t entrySize = sizeof(MeshcoreContact);
    uint16_t count = 0;
    MeshcoreContact contacts[kContactStoreCapacity];
};

size_t loadContacts(MeshcoreContact *out, size_t max);

bool saveContacts(const MeshcoreContact *contacts, size_t count);

} // namespace meshcompromise
