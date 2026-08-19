#pragma once

#include <cstddef>
#include <cstdint>

namespace meshcompromise
{

constexpr size_t kMeshcorePubKeySize = 32;
constexpr size_t kMeshcoreNameLength = 24;

struct MeshcoreContact {
    uint8_t pubKey[kMeshcorePubKeySize] = {0};
    char name[kMeshcoreNameLength] = {0};
    uint32_t nodeNum = 0;
    bool used = false;
};

class GroupTextSink
{
  public:
    virtual ~GroupTextSink() = default;
    virtual void onMeshcoreText(const char *text, size_t length) = 0;
    virtual void onMeshcoreDirectText(const MeshcoreContact &from, const char *text, size_t length) = 0;
    virtual void onMeshcoreContact(const MeshcoreContact &contact) = 0;
};

class MeshcoreSink
{
  public:
    virtual ~MeshcoreSink() = default;
    virtual bool sendGroupText(uint8_t channelIndex, const char *senderName, const char *text, size_t length) = 0;
    virtual bool sendDirectText(uint32_t nodeNum, const char *text, size_t length) = 0;
    virtual const MeshcoreContact *contactByNodeNum(uint32_t nodeNum) const = 0;
    virtual const MeshcoreContact *contactByName(const char *name) const = 0;
    virtual uint8_t channelCount() const = 0;
};

} // namespace meshcompromise
