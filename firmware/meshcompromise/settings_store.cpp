#include "meshcompromise/settings_store.h"

#include "FSCommon.h"
#include "configuration.h"

namespace meshcompromise
{

namespace
{
constexpr const char *kSettingsPath = "/meshcompromise/settings.bin";
}

bool loadSettings(BridgeSettings &out)
{
    if (!FSCom.exists(kSettingsPath))
        return false;

    auto file = FSCom.open(kSettingsPath, FILE_O_READ);
    if (!file)
        return false;

    StoredSettings stored;
    const size_t read = file.read(reinterpret_cast<uint8_t *>(&stored), sizeof(stored));
    file.close();

    if (read != sizeof(stored))
        return false;
    if (stored.magic != kSettingsMagic || stored.version != kSettingsVersion || stored.size != sizeof(BridgeSettings))
        return false;
    if (!validateSettings(stored.settings))
        return false;

    out = stored.settings;
    normalizeSettings(out);
    return true;
}

bool saveSettings(const BridgeSettings &settings)
{
    if (!validateSettings(settings))
        return false;

    FSCom.mkdir("/meshcompromise");

    auto file = FSCom.open(kSettingsPath, FILE_O_WRITE);
    if (!file)
        return false;

    StoredSettings stored;
    stored.settings = settings;

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
    file.close();

    if (written != sizeof(stored)) {
        LOG_WARN("MeshCompromise settings write was short (%u bytes)", static_cast<unsigned>(written));
        return false;
    }
    return true;
}

} // namespace meshcompromise
