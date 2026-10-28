// Lowest-level GPI0 MMIO write guard. Only the verified 0x01D4 pad TX bit
// may change; no direction, mux, reset, speaker-ID, or other GPIO writes.
#pragma once
#include "KernelProbe/GPIOProviderLease.hpp"
#include "KernelProbe/PowerGate.hpp"
#include "Transport/SPI/Intel/IntelGPIOOutput.hpp"
#include <libkern/OSAtomic.h>

namespace g614jvr_probe {
class GPIOWindow final : public cirrus::transport::intel::IntelGPIOWindow {
public:
    GPIOWindow(GPIOProviderLease& lease, IOService* owner, PowerGate& gate) : lease_(lease), owner_(owner), gate_(gate) {}
    ~GPIOWindow() override { if (owner_) { owner_->setProperty("CS_GPIO_WRITES", writes_, 64); owner_->setProperty("GPIO_WRITE_DENIED", denied_ ? kOSBooleanTrue : kOSBooleanFalse); } }
    bool prepare() {
        cirrus::transport::intel::IntelGPIOPad pad{};
        if (!lease_.address() || !cirrus::transport::intel::lookupIntelGPIOPad("INTC1085", 0x01d4, pad) || pad.memoryIndex != 4)
            return false;
        const uint32_t base = read32(0x0c);
        if (base == 0xffffffffU || base == 0 || uint64_t(base) + uint64_t(pad.pad) * 16 + 4 > lease_.length()) return false;
        offset_ = base + uint32_t(pad.pad) * 16; ready_ = true; return true;
    }
    uint32_t read32(uint32_t offset) override {
        auto* base = lease_.address();
        if (!gate_.active() || !base || (offset & 3) || offset > lease_.length() || lease_.length() - offset < 4) return 0xffffffffU;
        auto value = *reinterpret_cast<volatile uint32_t*>(base + offset); OSSynchronizeIO(); return value;
    }
    void write32(uint32_t offset, uint32_t value) override {
        auto* base = lease_.address();
        if (!gate_.active() || !base || !ready_ || offset != offset_) { denied_ = true; return; }
        const uint32_t before = read32(offset);
        if (before == 0xffffffffU || (before & ~1U) != (value & ~1U)) { denied_ = true; return; }
        *reinterpret_cast<volatile uint32_t*>(base + offset) = value; OSSynchronizeIO(); ++writes_;
    }
    bool denied() const { return denied_; }
    uint64_t writes() const { return writes_; }
private:
    GPIOProviderLease& lease_; IOService* owner_; PowerGate& gate_; uint32_t offset_ = 0; uint64_t writes_ = 0; bool ready_ = false, denied_ = false;
};
}
