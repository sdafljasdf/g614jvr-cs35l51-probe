#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSData.h>
#include <string.h>
#include "KernelGPI0Observer/GPI0ReadOnly.hpp"

// Independent passive candidate: no trigger is exported until access and
// lifecycle requirements are established. Do not load or deploy this target.
class G614JVRGPI0Observer final : public IOService {
    OSDeclareDefaultStructors(G614JVRGPI0Observer)
    using super = IOService;
public:
    bool start(IOService* provider) override;
private:
    IOReturn observe(IOService* provider);
};
OSDefineMetaClassAndStructors(G614JVRGPI0Observer, IOService)
extern "C" __attribute__((visibility("default"))) kern_return_t G614JVRGPI0Observer_start(kmod_info_t*, void*) { return KERN_SUCCESS; }
extern "C" __attribute__((visibility("default"))) kern_return_t G614JVRGPI0Observer_stop(kmod_info_t*, void*) { return KERN_SUCCESS; }

namespace {
class ConstWindow final : public gpi0_observer::Reader {
public:
    ConstWindow(const volatile uint8_t* base, size_t length) : base_(base), length_(length) {}
    bool read(uint32_t offset, uint32_t& value) override {
        if (!base_ || (offset & 3) || offset > length_ || length_ - offset < 4) return false;
        value = *reinterpret_cast<const volatile uint32_t*>(base_ + offset);
        OSSynchronizeIO();
        return true;
    }
private:
    const volatile uint8_t* base_; size_t length_;
};
}

bool G614JVRGPI0Observer::start(IOService* provider) {
    if (!super::start(provider)) return false;
    setProperty("OBSERVER_STATUS", "PASSIVE_NO_TRIGGER");
    registerService();
    return true;
}

IOReturn G614JVRGPI0Observer::observe(IOService* provider) {
    auto* path = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/GPI0");
    const bool same = path && path == provider;
    OSSafeReleaseNULL(path);
    auto* acpiPath = IORegistryEntry::fromPath("IOACPIPlane:/_SB/GPI0");
    const bool sameACPI = acpiPath && acpiPath == provider;
    OSSafeReleaseNULL(acpiPath);
    auto* name = provider ? OSDynamicCast(OSData, provider->getProperty("name")) : nullptr;
    if (!same || !sameACPI || !name || name->getLength() < 8 || name->getLength() > 9 ||
        memcmp(name->getBytesNoCopy(), "INTC1085", 8) ||
        (name->getLength() == 9 && static_cast<const uint8_t*>(name->getBytesNoCopy())[8] != 0))
        return kIOReturnUnsupported;
    auto* mem = provider->getDeviceMemoryWithIndex(4);
    if (!mem || mem->getLength() != 0x10000 || mem->getPhysicalAddress() != 0xe0690000ULL)
        return kIOReturnNotReady;
    // Mapping may not be used unless returned options actually retain both
    // the read-only protection and requested cache-inhibited setting.
    auto* map = provider->mapDeviceMemoryWithIndex(4, kIOMapReadOnly | kIOMapInhibitCache);
    if (!map) return kIOReturnNoMemory;
    const IOOptionBits options = map->getMapOptions();
    if (!(options & kIOMapReadOnly) || (options & kIOMapCacheMask) != kIOMapInhibitCache ||
        map->getLength() != 0x10000 || !map->getVirtualAddress()) {
        map->release(); return kIOReturnNotPermitted;
    }
    ConstWindow window(reinterpret_cast<const volatile uint8_t*>(map->getVirtualAddress()), map->getLength());
    gpi0_observer::Sample s{};
    const bool success = gpi0_observer::sample(window, map->getLength(), s);
    map->release();
    if (!success) return kIOReturnNotReadable;
    // Values remain raw: physical deselection and persistence are not inferred.
    setProperty("MAP_OPTIONS", uint64_t(options), 32);
    setProperty("RESOURCE4_PHYS", uint64_t(0xe0690000ULL), 64);
    setProperty("RESOURCE4_LENGTH", uint64_t(0x10000), 32);
    setProperty("REVID", uint64_t(s.revid), 32); setProperty("CAPLIST", uint64_t(s.caplist), 32);
    setProperty("PADBAR", uint64_t(s.padbar), 32); setProperty("PAD_STRIDE", uint64_t(s.stride), 32);
    setProperty("PADCFG_OFFSET", uint64_t(s.cfgOffset), 32);
    setProperty("PADCFG0_RAW", uint64_t(s.cfg0), 32); setProperty("PADCFG1_RAW", uint64_t(s.cfg1), 32);
    setProperty("PADOWN_RAW", uint64_t(s.padOwn), 32); setProperty("HOSTSW_RAW", uint64_t(s.hostOwn), 32);
    setProperty("PADLOCK_RAW", uint64_t(s.cfgLock), 32); setProperty("PADTXLOCK_RAW", uint64_t(s.txLock), 32);
    setProperty("CS1_DESELECTED_NOW", "UNKNOWN");
    return kIOReturnSuccess;
}
