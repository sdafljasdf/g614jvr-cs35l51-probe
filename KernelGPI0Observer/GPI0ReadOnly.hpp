#pragma once
#include <stdint.h>
#include <stddef.h>

// A read-only interface with no write method; no target register scan.
namespace gpi0_observer {
struct Reader {
    virtual ~Reader() = default;
    virtual bool read(uint32_t offset, uint32_t& value) = 0;
};
struct Sample {
    uint32_t revid{}, caplist{}, padbar{}, stride{}, cfgOffset{};
    uint32_t cfg0{}, cfg1{}, padOwn{}, hostOwn{}, cfgLock{}, txLock{};
    uint32_t padOwnOffset{}, padOwnShift{}, padOwnMask{};
};
inline bool sample(Reader& r, size_t length, Sample& s) {
    if (length != 0x10000) return false;
    if (!r.read(0, s.revid) || !r.read(4, s.caplist) || !r.read(0x0c, s.padbar)) return false;
    if (s.revid == 0xffffffffU || s.caplist == 0xffffffffU ||
        s.padbar < 0x200 || (s.padbar & 3) || s.padbar >= length) return false;
    const uint32_t revision = s.revid >> 16;
    // Fail closed outside the feature generations audited for this observer.
    if (revision < 0x92 || revision > 0x110) return false;
    s.stride = revision >= 0x92 ? 16 : 8;
    const uint64_t cfg = uint64_t(s.padbar) + 20U * s.stride;
    if (cfg + s.stride > length) return false;
    s.cfgOffset = uint32_t(cfg);
    if (revision >= 0x110) { s.padOwnOffset = 0xf0; s.padOwnShift = 0; s.padOwnMask = 7; }
    else { s.padOwnOffset = 0xa8; s.padOwnShift = 16; s.padOwnMask = 15; }
    return r.read(s.cfgOffset, s.cfg0) && r.read(s.cfgOffset + 4, s.cfg1) &&
           r.read(s.padOwnOffset, s.padOwn) && r.read(0x150, s.hostOwn) &&
           r.read(0x110, s.cfgLock) && r.read(0x114, s.txLock) &&
           s.cfg0 != 0xffffffffU && s.cfg1 != 0xffffffffU &&
           s.padOwn != 0xffffffffU && s.hostOwn != 0xffffffffU &&
           s.cfgLock != 0xffffffffU && s.txLock != 0xffffffffU;
}
}
