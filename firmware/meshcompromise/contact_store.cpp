#include "meshcompromise/contact_store.h"

#include <cstring>

#include "FSCommon.h"
#include "configuration.h"

namespace meshcompromise
{

namespace
{
constexpr const char *kContactsPath = "/meshcompromise/contacts.bin";
}

size_t loadContacts(MeshcoreContact *out, size_t max)
{
    if (out == nullptr || max == 0)
        return 0;
    if (!FSCom.exists(kContactsPath))
        return 0;

    auto file = FSCom.open(kContactsPath, FILE_O_READ);
    if (!file)
        return 0;

    StoredContacts stored;
    const size_t read = file.read(reinterpret_cast<uint8_t *>(&stored), sizeof(stored));
    file.close();

    if (read != sizeof(stored))
        return 0;
    if (stored.magic != kContactStoreMagic || stored.version != kContactStoreVersion)
        return 0;
    if (stored.entrySize != sizeof(MeshcoreContact))
        return 0;

    size_t restored = 0;
    for (size_t i = 0; i < stored.count && i < kContactStoreCapacity && restored < max; i++) {
        if (!stored.contacts[i].used || stored.contacts[i].nodeNum == 0)
            continue;
        out[restored++] = stored.contacts[i];
    }

    LOG_INFO("MeshCompromise restored %u MeshCore contact(s) from flash", static_cast<unsigned>(restored));
    return restored;
}

bool saveContacts(const MeshcoreContact *contacts, size_t count)
{
    if (contacts == nullptr)
        return false;

    FSCom.mkdir("/meshcompromise");

    auto file = FSCom.open(kContactsPath, FILE_O_WRITE);
    if (!file)
        return false;

    StoredContacts stored;
    for (size_t i = 0; i < count && i < kContactStoreCapacity; i++) {
        if (!contacts[i].used)
            continue;
        stored.contacts[stored.count++] = contacts[i];
    }

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
    file.close();

    if (written != sizeof(stored)) {
        LOG_WARN("MeshCompromise contact write was short (%u bytes)", static_cast<unsigned>(written));
        return false;
    }
    return true;
}

} // namespace meshcompromise
