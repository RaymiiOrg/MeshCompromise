#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>

#include "fake_sx_driver.h"
#include "meshcompromise/meshcore_radio.h"
#include "meshcompromise/meshcore_stack.h"
#include "virtual_air.h"

namespace meshcompromise
{

extern unsigned long hostMillis;

class HostClock : public mesh::MillisecondClock
{
  public:
    unsigned long getMillis() override { return hostMillis; }
};

class HostRtcClock : public mesh::RTCClock
{
  public:
    uint32_t getCurrentTime() override { return time_; }
    void setCurrentTime(uint32_t time) override { time_ = time; }
    void tick() override {}

  private:
    uint32_t time_ = 1715770351;
};

class HostRng : public mesh::RNG
{
  public:
    explicit HostRng(uint32_t seed) : state_((seed * 2654435761u) | 1u) {}

    void random(uint8_t *dest, size_t size) override
    {
        for (size_t i = 0; i < size; i++) {
            state_ = state_ * 1664525u + 1013904223u;
            dest[i] = static_cast<uint8_t>(state_ >> 24);
        }
    }

  private:
    uint32_t state_;
};

class TextCollector : public GroupTextSink
{
  public:
    std::vector<std::string> texts;
    std::vector<std::string> directs;
    std::vector<uint32_t> directFrom;
    std::vector<MeshcoreContact> contacts;

    void onMeshcoreText(const char *text, size_t length) override { texts.emplace_back(text, length); }

    void onMeshcoreDirectText(const MeshcoreContact &from, const char *text, size_t length) override
    {
        directs.emplace_back(text, length);
        directFrom.push_back(from.nodeNum);
    }

    void onMeshcoreContact(const MeshcoreContact &contact) override { contacts.push_back(contact); }
};

class HostNode
{
  public:
    FakeSxDriver driver;
    FakeHostRadio host;
    MeshcoreRadio radio{driver, host};
    HostClock clock;
    HostRtcClock rtc;
    HostRng rng;
    SimpleMeshTables tables;
    StaticPoolPacketManager manager{8};
    MeshcoreStack stack{radio, clock, rng, rtc, manager, tables};
    TextCollector collector;

    HostNode(uint32_t seed, const char *psk = kMeshcorePublicPsk) : rng(seed)
    {
        stack.self_id = mesh::LocalIdentity(&rng);
        stack.addChannelFromPsk(psk);
        stack.setTextSink(&collector);
        stack.begin();

        host.bind(driver);
        driver.active = meshtasticNarrowSlowProfile();
    }

    void takeRadio() { radio.arbiter().enterMeshcore(SwitchMode::Aligned, meshcoreDefaultProfile()); }

    void releaseRadio() { radio.arbiter().leaveMeshcore(SwitchMode::Aligned, host.profile); }

    void pump(int rounds = 80, unsigned long stepMs = 600)
    {
        for (int i = 0; i < rounds; i++) {
            hostMillis += stepMs;
            radio.arbiter().pumpMeshcore();
            stack.loop();
            if (radio.arbiter().meshcoreTxBusy())
                driver.irq = true;
        }
    }
};

inline std::vector<uint8_t> lastFrame(const FakeSxDriver &driver)
{
    if (driver.sent.empty())
        return {};
    return driver.sent.back().bytes;
}

inline bool carryOverAir(VirtualAir &air, HostNode &to, const std::vector<uint8_t> &bytes)
{
    air.transmit(meshcoreDefaultProfile(), bytes);
    return air.deliverTo(to.driver, meshcoreDefaultProfile(), bytes);
}

} // namespace meshcompromise
