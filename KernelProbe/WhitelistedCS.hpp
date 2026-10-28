// Dedicated external-CS guard. Does not locate, open or map a GPIO provider.
// Call only after validating G614JVR _CRS and exclusive GPIO provider ownership.
#pragma once
#include "Transport/SPI/Intel/IntelGPIOOutput.hpp"
#include "KernelProbe/GPIOWindow.hpp"
#include <string.h>

namespace g614jvr_probe {
class WhitelistedCS final : public cirrus::transport::SPIChipSelect {
    using Window = cirrus::transport::intel::IntelGPIOWindow;
    class GuardedWindow final : public Window {
    public:
        GPIOWindow* delegate = nullptr;
        uint32_t allowedOffset = 0;
        bool armed = false, rejected = false;
        uint32_t read32(uint32_t offset) override { return delegate->read32(offset); }
        void write32(uint32_t offset, uint32_t value) override {
            if (!armed || !delegate || offset != allowedOffset) { rejected = true; return; }
            uint32_t before = delegate->read32(offset);
            // Only the TX state bit of the one validated pad may change.
            if (before == 0xffffffffU || (before & ~1U) != (value & ~1U)) { rejected = true; return; }
            delegate->write32(offset, value);
        }
    } window_;
public:
    WhitelistedCS() = default;
    WhitelistedCS(const WhitelistedCS&) = delete;
    WhitelistedCS& operator=(const WhitelistedCS&) = delete;
    IOReturn bind(GPIOWindow& raw, size_t bytes, const char* acpiPath, const char* hid, uint16_t pin) {
        if (bound_ || !acpiPath || strcmp(acpiPath, "\\_SB.GPI0") || pin != 0x01d4)
            return kIOReturnNotPermitted;
        cirrus::transport::intel::IntelGPIOPad pad{};
        if (!cirrus::transport::intel::lookupIntelGPIOPad(hid, pin, pad)) return kIOReturnUnsupported;
        const uint32_t padbar = raw.read32(0x0c);
        if (padbar == 0xffffffffU || uint64_t(padbar) + uint64_t(pad.pad) * 16 + 4 > bytes)
            return kIOReturnUnsupported;
        window_.delegate = &raw;
        window_.allowedOffset = padbar + uint32_t(pad.pad) * 16;
        const auto result = output_.bind(window_, bytes, pad, true);
        if (result != kIOReturnSuccess) { window_.delegate = nullptr; return result; }
        bound_ = true;
        return kIOReturnSuccess;
    }
    IOReturn setSelected(bool selected) {
        if (!bound_) return kIOReturnNotReady;
        window_.rejected = false;
        window_.armed = true;
        const auto result = output_.setActive(selected);
        window_.armed = false;
        return (window_.rejected || window_.delegate->denied()) ? kIOReturnNotPermitted : result;
    }
    IOReturn setActive(bool active) override { return setSelected(active); }
    void unbind() { output_.unbind(); bound_ = false; window_.delegate = nullptr; }
private:
    cirrus::transport::intel::IntelGPIOOutput output_;
    bool bound_ = false;
};
}
