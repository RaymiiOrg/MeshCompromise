#include "meshcompromise/meshcore_radio.h"

#include "meshcompromise/airtime.h"

namespace meshcompromise
{

MeshcoreRadio::MeshcoreRadio(SxDriver &driver, HostRadio &host) : host_(host), arbiter_(driver, host) {}

void MeshcoreRadio::setMeshcoreProfile(const LoraProfile &profile)
{
    arbiter_.setMeshcoreProfile(profile);
}

void MeshcoreRadio::setTxPower(int8_t dbm)
{
    arbiter_.setTxPower(dbm);
}

int MeshcoreRadio::recvRaw(uint8_t *bytes, int sz)
{
    if (bytes == nullptr || sz <= 0)
        return 0;
    return static_cast<int>(arbiter_.takeRx(bytes, static_cast<size_t>(sz)));
}

uint32_t MeshcoreRadio::getEstAirtimeFor(int len_bytes)
{
    if (len_bytes < 0)
        return 0;
    return static_cast<uint32_t>(packetAirtimeMs(arbiter_.meshcoreProfile(), static_cast<uint16_t>(len_bytes)));
}

float MeshcoreRadio::packetScore(float snr, int packet_len)
{
    return meshcompromise::packetScore(snr, arbiter_.meshcoreProfile().spreadingFactor, packet_len);
}

bool MeshcoreRadio::startSendRaw(const uint8_t *bytes, int len)
{
    if (bytes == nullptr || len <= 0)
        return false;
    return arbiter_.queueTx(bytes, static_cast<size_t>(len));
}

bool MeshcoreRadio::isSendComplete()
{
    return arbiter_.txIdle();
}

void MeshcoreRadio::onSendFinished() {}

int MeshcoreRadio::getNoiseFloor() const
{
    return const_cast<HostRadio &>(host_).noiseFloor();
}

bool MeshcoreRadio::isInRecvMode() const
{
    return arbiter_.leaseHeld();
}

bool MeshcoreRadio::isReceiving()
{
    return arbiter_.rxAvailable() || arbiter_.meshcorePacketInProgress();
}

} // namespace meshcompromise
