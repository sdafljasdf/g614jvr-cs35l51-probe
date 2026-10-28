// Narrow synchronous adapter for a future audited hardware backend.
// This file provides no device open, GPIO, MMIO, reset, or kext integration.
#pragma once
#include "IdentityFrame.hpp"
namespace g614jvr_probe {
template<class Backend> class IdentityReadTransport {
public:
    explicit IdentityReadTransport(Backend& backend) : backend_(backend) {}
    bool externalSelectProvenInactive() { return backend_.prepareAndVerifyExternalSelect(); }
    bool readIdentity(unsigned cs, uint32_t address, uint32_t& value) {
        value = 0;
        if (failed_ || index_ >= 4 || cs != index_ / 2 || address != (index_ % 2 ? 4U : 0U)) {
            failed_ = true;
            return false;
        }
        IdentityFrame frame{};
        if (!makeIdentityReadFrame(address, frame)) { failed_ = true; return false; }
        uint8_t rx[10]{};
        ++index_; // one attempt, including failure: never retry
        if (!backend_.exchangeRead(cs, frame.bytes, rx, sizeof(rx))) { failed_ = true; return false; }
        value = decodeIdentityResponse(rx, sizeof(rx));
        if (!value || value == 0xffffffffU) failed_ = true;
        return true;
    }
private:
    Backend& backend_;
    unsigned index_ = 0;
    bool failed_ = false;
};
}
