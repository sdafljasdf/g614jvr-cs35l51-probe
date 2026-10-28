// Constrained LPSS BAR window. No write to reset and FIFO accepts only a
// pre-armed, exact CS35L4x identity read frame.
#pragma once
#include "Transport/SPI/Intel/LPSSSPIEngine.hpp"
#include "Research/G614JVR/IdentityFrame.hpp"
#include "KernelProbe/PowerGate.hpp"
#include <mach/mach_time.h>
#include <libkern/OSAtomic.h>

namespace g614jvr_probe {
class ProbeMMIO final : public cirrus::transport::intel::RegisterWindow {
public:
    ProbeMMIO(volatile uint8_t* base, size_t bytes, IOService* owner, PowerGate& gate) : base_(base), bytes_(bytes), owner_(owner), gate_(gate) {}
    ~ProbeMMIO() override { if (owner_) { owner_->setProperty("SPI_CONTROLLER_MMIO_WRITES", writes_, 64); owner_->setProperty("SPI_MMIO_DENIED", denied_ ? kOSBooleanTrue : kOSBooleanFalse); } }
    bool arm(uint32_t address) {
        if (armed_ || denied_ || !makeIdentityReadFrame(address, expected_)) return false;
        count_ = 0; armed_ = true; return true;
    }
    bool finishFrame() { const bool ok = armed_ && !denied_ && count_ == 10; armed_ = false; return ok; }
    bool denied() const { return denied_; }
    uint32_t read32(uint32_t offset) override {
        if (!gate_.active() || !base_ || (offset & 3) || offset > bytes_ || bytes_ - offset < 4) return 0xffffffffU;
        auto value = *reinterpret_cast<volatile uint32_t*>(base_ + offset); OSSynchronizeIO(); return value;
    }
    void write32(uint32_t offset, uint32_t value) override {
        if (!gate_.active() || !base_ || (offset & 3) || offset > bytes_ || bytes_ - offset < 4) { denied_ = true; return; }
        // After a rejection allow only controller stop and native-CS
        // deassertion; never clock more data or alter other controller state.
        if (denied_ && !((offset == 0x00 && !(value & (1U << 7))) || (offset == 0x224 && (value & 3U) == 3U))) return;
        switch (offset) {
        case 0x00: case 0x04: case 0x08: case 0x28: case 0x2c:
        case 0x200: case 0x224: case 0x238: break;
        case 0x10:
            if (!armed_ || count_ >= 10 || value != expected_.bytes[count_]) { denied_ = true; return; }
            ++count_; break;
        default: denied_ = true; return; // includes LPSS controller reset 0x204
        }
        *reinterpret_cast<volatile uint32_t*>(base_ + offset) = value; OSSynchronizeIO(); ++writes_;
    }
    uint64_t nowUs() override { uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000; }
    void delayUs(uint32_t us) override { IODelay(us); }
    uint64_t writes() const { return writes_; }
private:
    volatile uint8_t* base_; size_t bytes_; IOService* owner_; PowerGate& gate_; IdentityFrame expected_{};
    unsigned count_ = 0; uint64_t writes_ = 0; bool armed_ = false, denied_ = false;
};
}
