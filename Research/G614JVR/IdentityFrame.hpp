// Linux regmap-compatible identity read frames. No hardware interface.
#pragma once
#include <cstdint>
#include <cstddef>
namespace g614jvr_probe {
struct IdentityFrame {
    uint8_t bytes[10]{}; // 4-byte BE address + 2 padding + 4 receive clocks
};
inline bool makeIdentityReadFrame(uint32_t address, IdentityFrame& out) {
    if (address != 0 && address != 4) return false;
    out = {};
    out.bytes[0] = 0x80;
    out.bytes[3] = static_cast<uint8_t>(address);
    return true;
}
inline uint32_t decodeIdentityResponse(const uint8_t* rx, size_t length) {
    if (!rx || length != 10) return 0;
    return (uint32_t(rx[6]) << 24) | (uint32_t(rx[7]) << 16) | (uint32_t(rx[8]) << 8) | uint32_t(rx[9]);
}
}
