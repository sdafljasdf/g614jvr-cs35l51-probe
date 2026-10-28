// Independent, transport-injected identity-only policy. Not connected to a kext.
#pragma once
#include <cstdint>

namespace g614jvr_probe {
enum class Outcome { Pass, TransferError, InvalidResponse };
struct EndpointResult { uint32_t device = 0; uint32_t revision = 0; bool deviceRead = false; bool revisionRead = false; };
struct Result { Outcome outcome = Outcome::TransferError; EndpointResult endpoints[2]{}; unsigned completedReads = 0; };

// Transport must implement bool readIdentity(unsigned cs, uint32_t reg, uint32_t& value).
// No write interface exists here; this does not establish that a supplied transport is safe.
template<class Transport> Result run(Transport& transport) {
    Result result{};
    for (unsigned cs = 0; cs < 2; ++cs) {
        auto& endpoint = result.endpoints[cs];
        for (unsigned i = 0; i < 2; ++i) {
            uint32_t value = 0;
            if (!transport.readIdentity(cs, i ? 4U : 0U, value)) return result;
            ++result.completedReads;
            if (i == 0) { endpoint.device = value; endpoint.deviceRead = true; }
            else { endpoint.revision = value; endpoint.revisionRead = true; }
            if (value == 0 || value == 0xffffffffU) {
                result.outcome = Outcome::InvalidResponse;
                return result;
            }
        }
    }
    result.outcome = Outcome::Pass;
    return result;
}
}
