#include "meshcompromise/contact_directory.h"

#include <cstdio>
#include <cstring>

#include "NodeDB.h"
#include "configuration.h"
#include "gps/RTC.h"

namespace meshcompromise
{

bool ContactDirectory::registerContact(const MeshcoreContact &contact)
{
    if (nodeDB == nullptr) {
        LOG_WARN("MeshCompromise cannot register contact %s, no NodeDB", contact.name);
        return false;
    }

    meshtastic_NodeInfoLite *node = nodeDB->getOrCreateMeshNode(contact.nodeNum);
    if (node == nullptr) {
        LOG_WARN("MeshCompromise could not allocate a node for MeshCore contact %s", contact.name);
        return false;
    }

    node->public_key.size = 0;
    snprintf(node->long_name, sizeof(node->long_name), "%s (MeshCore)", contact.name);
    strncpy(node->short_name, contact.name, sizeof(node->short_name) - 1);
    node->short_name[sizeof(node->short_name) - 1] = 0;
    node->hw_model = meshtastic_HardwareModel_PRIVATE_HW;
    node->last_heard = getValidTime(RTCQualityFromNet);
    nodeInfoLiteSetBit(node, NODEINFO_BITFIELD_HAS_USER_MASK, true);

    bool tracked = false;
    for (size_t i = 0; i < kMaxTrackedContacts && i < count_; i++) {
        if (known_[i] == contact.nodeNum) {
            tracked = true;
            break;
        }
    }

    if (!tracked) {
        if (count_ < kMaxTrackedContacts)
            known_[count_] = contact.nodeNum;
        count_++;
    }

    LOG_INFO("MeshCompromise learned MeshCore contact %s as 0x%x (%u known)", contact.name, contact.nodeNum,
             static_cast<unsigned>(count_));
    return true;
}

} // namespace meshcompromise
