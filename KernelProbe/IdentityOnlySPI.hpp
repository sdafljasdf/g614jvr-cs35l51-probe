// Probe-only identity transfer adapter. No amp write or arbitrary read method.
#pragma once
#include "Transport/SPI/Intel/LPSSSPIEngine.hpp"
#include "KernelProbe/WhitelistedCS.hpp"
#include "Research/G614JVR/IdentityFrame.hpp"
#include "KernelProbe/ProbeMMIO.hpp"

namespace g614jvr_probe {
class IdentityOnlySPI final {
public:
    struct Result { uint32_t device[2]{}, revision[2]{}; IOReturn transfer[4]{}; unsigned attempted{0}, completed{0}, reads{0}; IOReturn error{kIOReturnNotReady}, cleanup{kIOReturnNotReady}; };
    IdentityOnlySPI(cirrus::transport::intel::LPSSSPIEngine& engine, WhitelistedCS& cs, ProbeMMIO& mmio, PowerGate& power)
        : engine_(engine), cs_(cs), mmio_(mmio), power_(power) {}
    // The caller must construct engine with allowControllerReset=false and hold
    // both controller and GPIO provider ownership for this entire operation.
    Result run() {
        Result result{};
        if (engine_.allowsControllerReset()) {
            result.error = kIOReturnNotPermitted;
            return result;
        }
        if (!power_.begin(true)) { result.error = kIOReturnNotReady; return result; }
        auto status = cs_.setSelected(false);
        power_.end();
        if (status == kIOReturnSuccess) {
            for (unsigned i = 0; i < 4; ++i) {
                if (!power_.begin(true)) { status = kIOReturnNotReady; break; }
                uint32_t value = 0;
                status = readNext(i / 2, (i & 1) ? 4 : 0, value);
                power_.end();
                result.attempted = index_;
                result.transfer[i] = status;
                if (status == kIOReturnSuccess) {
                    ++result.completed;
                    if (i & 1) result.revision[i / 2] = value; else result.device[i / 2] = value;
                }
                if (status != kIOReturnSuccess || value == 0 || value == 0xffffffffU) {
                    if (status == kIOReturnSuccess) status = kIOReturnIOError;
                    break;
                }
                ++result.reads;
            }
        }
        // A failed deassertion is a failure, even if the four reads succeeded.
        if (power_.begin(true)) {
            result.cleanup = cs_.setSelected(false);
            power_.end();
        } // Do not touch GPIO after either power domain becomes unavailable.
        result.error = status == kIOReturnSuccess ? result.cleanup : status;
        return result;
    }
private:
    IOReturn readNext(unsigned cs, uint32_t address, uint32_t& value) {
        if (failed_ || index_ >= 4 || cs != index_ / 2 || address != ((index_ & 1) ? 4U : 0U))
            return kIOReturnNotPermitted;
        IdentityFrame frame{};
        if (!makeIdentityReadFrame(address, frame)) return kIOReturnNotPermitted;
        if (!mmio_.arm(address)) return kIOReturnNotPermitted;
        uint8_t rx[10]{};
        ++index_; // count before transfer: no retry on any error
        const cirrus::transport::SPIEndpoint endpoint{cs, 4000000, 0};
        const auto ret = engine_.exchange(endpoint, frame.bytes, rx, sizeof(rx), cs ? &cs_ : nullptr);
        const bool allowed = mmio_.finishFrame();
        if (ret != kIOReturnSuccess || !allowed) { failed_ = true; return ret != kIOReturnSuccess ? ret : kIOReturnNotPermitted; }
        value = decodeIdentityResponse(rx, sizeof(rx));
        if (!value || value == 0xffffffffU) failed_ = true;
        return kIOReturnSuccess;
    }
    cirrus::transport::intel::LPSSSPIEngine& engine_;
    WhitelistedCS& cs_;
    ProbeMMIO& mmio_;
    PowerGate& power_;
    unsigned index_ = 0;
    bool failed_ = false;
};
}
