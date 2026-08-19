#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "host_mesh.h"
#include "meshcompromise/airtime.h"
#include "meshcompromise/slice_scheduler.h"

using namespace meshcompromise;

namespace
{

struct Transmission {
    LoraProfile profile;
    const std::vector<uint8_t> *frame = nullptr;
    uint32_t preambleEndsMs = 0;
    uint32_t endsMs = 0;
    bool isMeshcore = false;
    bool detected = false;
    bool finished = false;
};

struct Reference {
    LoraProfile profile;
    uint32_t periodMs = 0;
    uint32_t offsetMs = 0;
    std::vector<uint8_t> frame;
    uint32_t sentCount = 0;
    uint32_t capturedCount = 0;

    bool firesAt(uint32_t nowMs) const { return nowMs >= offsetMs && (nowMs - offsetMs) % periodMs == 0; }

    float captureRate() const { return sentCount == 0 ? 0.0f : static_cast<float>(capturedCount) / sentCount; }
};

LoraProfile alignedMeshtastic()
{
    LoraProfile profile = meshcoreDefaultProfile();
    profile.syncWord = 0x2b;
    profile.preambleSymbols = 16;
    return profile;
}

LoraProfile longFastMeshtastic()
{
    LoraProfile profile;
    profile.frequencyMhz = 869.525f;
    profile.bandwidthKhz = 250.0f;
    profile.spreadingFactor = 11;
    profile.codingRate = 5;
    profile.syncWord = 0x2b;
    profile.preambleSymbols = 16;
    return profile;
}

class Bridge
{
  public:
    FakeSxDriver driver;
    FakeHostRadio host;
    MeshcoreRadio radio{driver, host};
    SliceScheduler scheduler;

    explicit Bridge(const LoraProfile &meshtastic) : scheduler(radio.arbiter(), SliceConfig())
    {
        host.bind(driver);
        host.profile = meshtastic;
        driver.active = meshtastic;
        scheduler.setProfiles(meshtastic, meshcoreDefaultProfile());
    }
};

struct RunResult {
    Reference meshcore;
    Reference meshtastic;
    SliceStats stats;
    SwitchMode mode = SwitchMode::Split;
};

std::vector<uint8_t> aMeshcoreFrame()
{
    HostNode node(7);
    node.takeRadio();
    node.stack.sendGroupText(0, "REF", "reference traffic", 17);
    node.pump();
    return lastFrame(node.driver);
}

RunResult run(const LoraProfile &meshtasticProfile, uint32_t durationMs, uint32_t meshcorePeriodMs,
              uint32_t meshtasticPeriodMs, bool bridgeEnabled = true)
{
    Bridge bridge(meshtasticProfile);
    if (!bridgeEnabled) {
        SliceConfig off = bridge.scheduler.config();
        off.enabled = false;
        bridge.scheduler.setConfig(off);
    }

    RunResult result;
    result.meshcore.profile = meshcoreDefaultProfile();
    result.meshcore.periodMs = meshcorePeriodMs;
    result.meshcore.offsetMs = 100;
    result.meshcore.frame = aMeshcoreFrame();

    result.meshtastic.profile = meshtasticProfile;
    result.meshtastic.periodMs = meshtasticPeriodMs;
    result.meshtastic.offsetMs = 137;
    result.meshtastic.frame = std::vector<uint8_t>(40, 0x5A);

    Reference *references[] = {&result.meshcore, &result.meshtastic};
    std::vector<Transmission> inFlight;

    for (uint32_t now = 0; now < durationMs; now++) {
        for (Reference *reference : references) {
            if (!reference->firesAt(now))
                continue;
            reference->sentCount++;

            Transmission transmission;
            transmission.profile = reference->profile;
            transmission.frame = &reference->frame;
            transmission.isMeshcore = reference == &result.meshcore;
            transmission.preambleEndsMs = now + static_cast<uint32_t>(preambleTimeMs(reference->profile));
            transmission.endsMs =
                now + static_cast<uint32_t>(packetAirtimeMs(reference->profile,
                                                            static_cast<uint16_t>(reference->frame.size())));
            inFlight.push_back(transmission);
        }

        bool audiblePreamble = false;
        bool audiblePacket = false;
        bool meshtasticReceiving = false;

        for (Transmission &transmission : inFlight) {
            if (now > transmission.endsMs)
                continue;

            const bool tuned = VirtualAir::hearable(transmission.profile, bridge.driver.active);

            if (tuned && now <= transmission.preambleEndsMs) {
                transmission.detected = true;
                if (transmission.isMeshcore)
                    audiblePreamble = true;
            }

            if (!transmission.detected)
                continue;

            if (!tuned) {
                transmission.detected = false;
                continue;
            }

            if (now < transmission.endsMs) {
                if (transmission.isMeshcore)
                    audiblePacket = true;
                else
                    meshtasticReceiving = true;
            } else if (!transmission.finished) {
                transmission.finished = true;
                if (transmission.isMeshcore)
                    bridge.driver.deliver(*transmission.frame);
                else
                    result.meshtastic.capturedCount++;
            }
        }

        bridge.driver.cadDetected = audiblePreamble;
        bridge.driver.inboundInProgress = audiblePacket;
        bridge.host.receiving = meshtasticReceiving;
        bridge.scheduler.tick(now);

        uint8_t drained[256] = {0};
        const int length = bridge.radio.recvRaw(drained, static_cast<int>(sizeof(drained)));
        if (length <= 0)
            continue;

        for (Reference *reference : references) {
            if (static_cast<size_t>(length) != reference->frame.size())
                continue;
            if (std::memcmp(drained, reference->frame.data(), reference->frame.size()) != 0)
                continue;
            reference->capturedCount++;
            break;
        }
    }

    result.stats = bridge.scheduler.stats();
    result.mode = bridge.scheduler.mode();
    return result;
}

constexpr uint32_t kWindowMs = 120000;
constexpr uint32_t kMeshcorePeriodMs = 1103;
constexpr uint32_t kMeshtasticPeriodMs = 1699;
constexpr uint32_t kQuietMeshcorePeriodMs = 9701;
constexpr uint32_t kQuietMeshtasticPeriodMs = 5297;

RunResult aligned()
{
    return run(alignedMeshtastic(), kWindowMs, kMeshcorePeriodMs, kMeshtasticPeriodMs);
}

RunResult split()
{
    return run(longFastMeshtastic(), kWindowMs, kMeshcorePeriodMs, kMeshtasticPeriodMs);
}

RunResult quiet()
{
    return run(alignedMeshtastic(), kWindowMs, kQuietMeshcorePeriodMs, kQuietMeshtasticPeriodMs);
}

RunResult bridgeOff()
{
    return run(alignedMeshtastic(), kWindowMs, kQuietMeshcorePeriodMs, kQuietMeshtasticPeriodMs, false);
}

} // namespace

TEST(CaptureRate, AlignedModeIsSelectedWhenBothProtocolsSharePhyParameters)
{
    EXPECT_EQ(aligned().mode, SwitchMode::Aligned);
}

TEST(CaptureRate, SplitModeIsSelectedWhenMeshtasticUsesADifferentPreset)
{
    EXPECT_EQ(split().mode, SwitchMode::Split);
}

TEST(CaptureRate, BothReferenceNodesTransmitOnSchedule)
{
    const RunResult result = aligned();
    EXPECT_EQ(result.meshcore.sentCount, (kWindowMs - 100) / kMeshcorePeriodMs + 1);
    EXPECT_EQ(result.meshtastic.sentCount, (kWindowMs - 137) / kMeshtasticPeriodMs + 1);
}

TEST(CaptureRate, AlignedModeHearsMostMeshcoreTrafficUnderLoad)
{
    EXPECT_GT(aligned().meshcore.captureRate(), 0.80f);
}

TEST(CaptureRate, AlignedModeBeatsSplitModeForMeshcore)
{
    EXPECT_GT(aligned().meshcore.captureRate(), split().meshcore.captureRate());
}

TEST(CaptureRate, MeshtasticKeepsMostOfItsReceptionUnderLoad)
{
    EXPECT_GT(aligned().meshtastic.captureRate(), 0.65f);
}

TEST(CaptureRate, TypicalTrafficCostsMeshtasticVeryLittle)
{
    const RunResult result = quiet();
    EXPECT_GT(result.meshtastic.captureRate(), 0.85f);
    EXPECT_LT(result.stats.meshcoreDutyCycle(), 0.10f);
}

TEST(CaptureRate, TypicalTrafficIsStillCaptured)
{
    EXPECT_GT(quiet().meshcore.captureRate(), 0.85f);
}

TEST(CaptureRate, EveryCapturedPacketCameFromAPositiveDetection)
{
    const RunResult result = aligned();
    EXPECT_EQ(result.stats.cadPositive, result.meshcore.capturedCount);
    EXPECT_GT(result.stats.cadNegative, result.stats.cadPositive);
}

TEST(CaptureRate, TheDwellAlwaysEndsBeforeItsTimeout)
{
    EXPECT_EQ(aligned().stats.dwellTimeouts, 0u);
    EXPECT_EQ(quiet().stats.dwellTimeouts, 0u);
}

TEST(CaptureRate, DisablingTheBridgeMissesEveryMeshcoreTransmission)
{
    const RunResult result = bridgeOff();
    EXPECT_GT(result.meshcore.sentCount, 0u);
    EXPECT_EQ(result.meshcore.capturedCount, 0u);
    EXPECT_EQ(result.stats.meshcoreListenMs, 0u);
}

TEST(CaptureRate, DisablingTheBridgeGivesMeshtasticEverything)
{
    EXPECT_FLOAT_EQ(bridgeOff().meshtastic.captureRate(), 1.0f);
}
