#pragma once

#include "meshcompromise/meshcore_sink.h"

namespace meshcompromise
{

class ContactDirectory
{
  public:
    bool registerContact(const MeshcoreContact &contact);
    uint32_t count() const { return count_; }

  private:
    static constexpr size_t kMaxTrackedContacts = 16;

    uint32_t count_ = 0;
    uint32_t known_[kMaxTrackedContacts] = {0};
};

} // namespace meshcompromise
