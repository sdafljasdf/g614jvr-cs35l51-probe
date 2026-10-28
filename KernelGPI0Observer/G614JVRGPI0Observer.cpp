#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOUserClient.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSData.h>
#include <string.h>
#include "KernelGPI0Observer/GPI0ReadOnly.hpp"
#include "KernelGPI0Observer/GPI0Protocol.h"

// Independent passive candidate. Live trigger remains compiled out until
// process-death / power lifetime and provider safety are proven.
class G614JVRGPI0Observer final : public IOService {
    OSDeclareDefaultStructors(G614JVRGPI0Observer)
    using super = IOService;
public:
    bool start(IOService* provider) override;
    IOReturn newUserClient(task_t task, void* security, UInt32 type, OSDictionary* properties, IOUserClient** handler) override;
    IOReturn observe(GPI0ObservationResult& result);
private:
    volatile SInt32 active_ = 0;
};
OSDefineMetaClassAndStructors(G614JVRGPI0Observer, IOService)
extern "C" __attribute__((visibility("default"))) kern_return_t G614JVRGPI0Observer_start(kmod_info_t*, void*) { return KERN_SUCCESS; }
extern "C" __attribute__((visibility("default"))) kern_return_t G614JVRGPI0Observer_stop(kmod_info_t*, void*) { return KERN_SUCCESS; }

class G614JVRGPI0UserClient final : public IOUserClient {
    OSDeclareDefaultStructors(G614JVRGPI0UserClient)
    using super = IOUserClient;
public:
    bool start(IOService* provider) override;
    void free() override;
    IOReturn clientClose() override;
    IOReturn clientDied() override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* args,
                           IOExternalMethodDispatch*, OSObject*, void*) override;
private:
    G614JVRGPI0Observer* owner_ = nullptr;
    // Bit 0 closing, bit 1 active, bit 2 consumed. Atomic admission prevents
    // a new observation from being admitted after clientClose/clientDied.
    volatile SInt32 state_ = 0;
};
OSDefineMetaClassAndStructors(G614JVRGPI0UserClient, IOUserClient)

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

IOReturn G614JVRGPI0Observer::observe(GPI0ObservationResult& result) {
    if (!OSCompareAndSwap(0, 1, &active_)) return kIOReturnBusy;
    struct ActiveCleanup { volatile SInt32* flag; ~ActiveCleanup() { OSCompareAndSwap(1, 0, flag); } } active{&active_};
    IOService* provider = getProvider();
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
    result.resource_phys = 0xe0690000ULL; result.resource_length = 0x10000;
    result.map_options = options; result.revid = s.revid; result.caplist = s.caplist;
    result.padbar = s.padbar; result.stride = s.stride; result.cfg_offset = s.cfgOffset;
    result.cfg0 = s.cfg0; result.cfg1 = s.cfg1; result.padown = s.padOwn;
    result.hostsw = s.hostOwn; result.cfglock = s.cfgLock; result.txlock = s.txLock;
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

IOReturn G614JVRGPI0Observer::newUserClient(task_t task, void* security, UInt32 type,
                                             OSDictionary* properties, IOUserClient** handler) {
    if (!handler || type != 0 || IOUserClient::clientHasPrivilege(security, kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
        return kIOReturnNotPermitted;
    auto* client = new G614JVRGPI0UserClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, security, type, properties)) { client->release(); return kIOReturnError; }
    if (!client->attach(this)) { client->release(); return kIOReturnError; }
    if (!client->start(this)) { client->detach(this); client->release(); return kIOReturnError; }
    *handler = client; return kIOReturnSuccess;
}
bool G614JVRGPI0UserClient::start(IOService* provider) {
    if (!super::start(provider)) return false;
    owner_ = OSDynamicCast(G614JVRGPI0Observer, provider);
    if (!owner_) return false;
    owner_->retain(); return true;
}
void G614JVRGPI0UserClient::free() { OSSafeReleaseNULL(owner_); super::free(); }
IOReturn G614JVRGPI0UserClient::clientClose() {
    SInt32 previous;
    do { previous = OSAddAtomic(0, &state_); }
    while (!(previous & 1) && !OSCompareAndSwap(uint32_t(previous), uint32_t(previous | 1), &state_));
    terminate(); return kIOReturnSuccess;
}
IOReturn G614JVRGPI0UserClient::clientDied() { return clientClose(); }
IOReturn G614JVRGPI0UserClient::externalMethod(uint32_t selector, IOExternalMethodArguments* args,
                                               IOExternalMethodDispatch*, OSObject*, void*) {
    if (selector != kG0RunObservation || !args || !owner_ || args->scalarInputCount || args->scalarOutputCount ||
        args->structureInputSize || args->structureOutputSize != sizeof(GPI0ObservationResult) || !args->structureOutput)
        return kIOReturnBadArgument;
    if (!OSCompareAndSwap(0, 6, &state_)) return kIOReturnBusy;
    retain();
    GPI0ObservationResult result{};
    IOReturn status = kIOReturnNotReady;
#if defined(G0_UNREVIEWED_LIVE_TRIGGER)
    if (!(OSAddAtomic(0, &state_) & 1)) status = owner_->observe(result);
#endif
    if (status == kIOReturnSuccess && !(OSAddAtomic(0, &state_) & 1)) {
        memcpy(args->structureOutput, &result, sizeof(result));
        args->structureOutputSize = sizeof(result);
    }
    SInt32 previous;
    do { previous = OSAddAtomic(0, &state_); }
    while (!OSCompareAndSwap(uint32_t(previous), uint32_t(previous & ~2), &state_));
    release();
    return status;
}
