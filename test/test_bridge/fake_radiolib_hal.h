#pragma once

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <RadioLib.h>

namespace meshcompromise
{

struct SpiCall {
    uint8_t opcode = 0;
    uint16_t address = 0;
    std::vector<uint8_t> payload;
};

class FakeSx126xHal : public RadioLibHal
{
  public:
    FakeSx126xHal() : RadioLibHal(0, 1, 0, 1, 0, 1)
    {
        const char *version = RADIOLIB_SX1262_CHIP_TYPE;
        for (uint16_t i = 0; i < 6; i++)
            registers[RADIOLIB_SX126X_REG_VERSION_STRING + i] = static_cast<uint8_t>(version[i]);
    }

    void init() override {}
    void term() override {}
    void pinMode(uint32_t, uint32_t) override {}
    void digitalWrite(uint32_t, uint32_t) override {}
    uint32_t digitalRead(uint32_t pin) override
    {
        if (pin == irqPin)
            return irqPinHigh ? 1 : 0;
        return busy ? 1 : 0;
    }
    void attachInterrupt(uint32_t, void (*)(void), uint32_t) override { interrupts++; }
    void detachInterrupt(uint32_t) override { interrupts--; }
    void delay(RadioLibTime_t ms) override { now += ms; }
    void delayMicroseconds(RadioLibTime_t us) override { now += us / 1000; }
    RadioLibTime_t millis() override { return now; }
    RadioLibTime_t micros() override { return now * 1000; }
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }
    void spiBegin() override {}
    void spiBeginTransaction() override {}
    void spiEndTransaction() override {}
    void spiEnd() override {}
    void yield() override {}

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override
    {
        memset(in, 0, len);
        for (size_t i = 0; i < len; i++)
            in[i] = kStatusOk;

        if (len == 0)
            return;

        SpiCall call;
        call.opcode = out[0];

        if (failOpcode != 0 && call.opcode == failOpcode) {
            for (size_t i = 0; i < len; i++)
                in[i] = kStatusFailed;
            calls.push_back(call);
            return;
        }

        if (call.opcode == RADIOLIB_SX126X_CMD_WRITE_REGISTER && len >= 3) {
            call.address = static_cast<uint16_t>((out[1] << 8) | out[2]);
            for (size_t i = 3; i < len; i++) {
                call.payload.push_back(out[i]);
                registers[static_cast<uint16_t>(call.address + (i - 3))] = out[i];
            }
        } else if (call.opcode == RADIOLIB_SX126X_CMD_READ_REGISTER && len >= 4) {
            registerReads++;
            call.address = static_cast<uint16_t>((out[1] << 8) | out[2]);
            for (size_t i = 4; i < len; i++)
                in[i] = registers.count(static_cast<uint16_t>(call.address + (i - 4)))
                            ? registers[static_cast<uint16_t>(call.address + (i - 4))]
                            : 0;
        } else if (call.opcode == RADIOLIB_SX126X_CMD_SET_PACKET_TYPE && len >= 2) {
            packetType = out[1];
            call.payload.push_back(out[1]);
        } else if (call.opcode == RADIOLIB_SX126X_CMD_GET_PACKET_TYPE && len >= 3) {
            in[2] = packetType;
        } else if (call.opcode == RADIOLIB_SX126X_CMD_GET_DEVICE_ERRORS && len >= 4) {
            in[2] = 0;
            in[3] = 0;
        } else if (call.opcode == RADIOLIB_SX126X_CMD_READ_BUFFER && len >= 3) {
            for (size_t i = 3; i < len && (i - 3) < rxBuffer.size(); i++)
                in[i] = rxBuffer[i - 3];
        } else if (call.opcode == RADIOLIB_SX126X_CMD_GET_IRQ_STATUS && len >= 4) {
            in[2] = static_cast<uint8_t>(irqFlags >> 8);
            in[3] = static_cast<uint8_t>(irqFlags & 0xFF);
        } else if (call.opcode == RADIOLIB_SX126X_CMD_GET_RX_BUFFER_STATUS && len >= 4) {
            in[2] = static_cast<uint8_t>(rxBuffer.size());
            in[3] = 0;
        } else {
            for (size_t i = 1; i < len; i++)
                call.payload.push_back(out[i]);
        }

        calls.push_back(call);
    }

    size_t countOpcode(uint8_t opcode) const
    {
        size_t total = 0;
        for (const SpiCall &call : calls)
            if (call.opcode == opcode)
                total++;
        return total;
    }

    size_t countRegisterWrites(uint16_t address) const
    {
        size_t total = 0;
        for (const SpiCall &call : calls)
            if (call.opcode == RADIOLIB_SX126X_CMD_WRITE_REGISTER && call.address == address)
                total++;
        return total;
    }

    bool sawOpcode(uint8_t opcode) const { return countOpcode(opcode) > 0; }

    uint8_t registerValue(uint16_t address) { return registers.count(address) ? registers[address] : 0; }

    void clear() { calls.clear(); }

    std::vector<SpiCall> calls;
    std::map<uint16_t, uint8_t> registers;
    std::vector<uint8_t> rxBuffer;
    size_t registerReads = 0;
    uint8_t packetType = 0;
    uint16_t irqFlags = 0;
    RadioLibTime_t now = 0;
    bool busy = false;
    bool irqPinHigh = false;
    uint32_t irqPin = 1;
    uint8_t failOpcode = 0;
    int interrupts = 0;

  private:
    static constexpr uint8_t kStatusOk = 0xA2;
    static constexpr uint8_t kStatusFailed = 0xAA;
};

} // namespace meshcompromise
