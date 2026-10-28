#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSData.h>
#include <string.h>
#include "KernelGPI0Observer/GPI0ReadOnly.hpp"
#include "KernelGPI0Observer/GPI0Protocol.h"

// Passive at load. The only hardware entry is the root-only synchronous
// user-client selector, intended for the separate supervised experiment.
class G614JVRGPI0Observer final : public IOService {
    OSDeclareDefaultStructors(G614JVRGPI0Observer)
    using super = IOService;
public:
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    void free() override;
    IOReturn newUserClient(task_t task, void* security, UInt32 type, OSDictionary* properties, IOUserClient** handler) override;
    IOReturn observe(GPI0ObservationResult& result, volatile SInt32* clientState);
private:
    IOService* resourceProvider_ = nullptr;
    volatile SInt32 active_ = 0; // bit 0 observing, bit 1 service stopping
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
    void finishClosing();
    G614JVRGPI0Observer* owner_ = nullptr;
    // Bit 0 closing, bit 1 active, bit 2 consumed. Atomic admission prevents
    // a new observation from being admitted after clientClose/clientDied.
    volatile SInt32 state_ = 0;
};
OSDefineMetaClassAndStructors(G614JVRGPI0UserClient, IOUserClient)

namespace {
class ConstWindow final : public gpi0_observer::Reader {
public:
    ConstWindow(const volatile uint8_t* base, size_t length, volatile SInt32* clientState, volatile SInt32* serviceState)
        : base_(base), length_(length), clientState_(clientState), serviceState_(serviceState) {}
    bool read(uint32_t offset, uint32_t& value) override {
        if ((OSAddAtomic(0,clientState_)&1) || (OSAddAtomic(0,serviceState_)&2) ||
            !base_ || (offset & 3) || offset > length_ || length_ - offset < 4 || reads_>=9) return false;
        value = *reinterpret_cast<const volatile uint32_t*>(base_ + offset);
        OSSynchronizeIO();
        ++reads_;
        return true;
    }
    uint32_t reads() const { return reads_; }
private:
    const volatile uint8_t* base_; size_t length_; uint32_t reads_=0;
    volatile SInt32* clientState_; volatile SInt32* serviceState_;
};
class MapScope final {
public:
    explicit MapScope(IOMemoryMap* map) : map_(map) {}
    ~MapScope() { if (map_) map_->release(); }
    MapScope(const MapScope&)=delete; MapScope& operator=(const MapScope&)=delete;
private: IOMemoryMap* map_;
};
}

bool G614JVRGPI0Observer::start(IOService* provider) {
    if (!super::start(provider)) return false;
    if (!OSDynamicCast(IOACPIPlatformDevice,provider)) return false;
    resourceProvider_=provider; resourceProvider_->retain();
    setProperty("OBSERVER_STATUS", "PASSIVE_WAITING_FOR_SUPERVISED_TRIGGER");
    setProperty("G0_TRIGGER_PROTOCOL", "READY_G0_SUPERVISED_V2");
    registerService();
    return true;
}
void G614JVRGPI0Observer::stop(IOService* provider) {
    OSBitOrAtomic(2,reinterpret_cast<volatile UInt32*>(&active_));
    super::stop(provider);
}
void G614JVRGPI0Observer::free() { OSSafeReleaseNULL(resourceProvider_); super::free(); }

IOReturn G614JVRGPI0Observer::observe(GPI0ObservationResult& result, volatile SInt32* clientState) {
    if (!OSCompareAndSwap(0, 1, &active_)) return kIOReturnBusy;
    struct ActiveCleanup { volatile SInt32* flag; ~ActiveCleanup() { OSBitAndAtomic(~1U,reinterpret_cast<volatile UInt32*>(flag)); } } active{&active_};
    IOService* provider = resourceProvider_;
    if (!provider || provider->isInactive() || isInactive() || (OSAddAtomic(0,clientState)&1)) return kIOReturnNotReady;
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
    if (provider->getDeviceMemoryCount()<5 || provider->isOpen() || !provider->open(this,0,nullptr))
        return kIOReturnExclusiveAccess;
    struct ProviderCleanup { IOService* provider; IOService* client; ~ProviderCleanup() { provider->close(client); } } providerCleanup{provider,this};
    auto* mem = provider->getDeviceMemoryWithIndex(4);
    if (!mem || mem->getLength() != 0x10000 || mem->getPhysicalAddress() != 0xe0690000ULL)
        return kIOReturnNotReady;
    result.resource_phys=mem->getPhysicalAddress(); result.resource_length=mem->getLength();
    result.map_options_requested=kIOMapReadOnly|kIOMapInhibitCache;
    if ((OSAddAtomic(0,clientState)&1) || (OSAddAtomic(0,&active_)&2)) return kIOReturnAborted;
    // Mapping may not be used unless returned options actually retain both
    // the read-only protection and requested cache-inhibited setting.
    auto* map = provider->mapDeviceMemoryWithIndex(4, kIOMapReadOnly | kIOMapInhibitCache);
    if (!map) return kIOReturnNoMemory;
    MapScope mapping(map);
    const IOOptionBits options = map->getMapOptions();
    result.map_options=options;
    if (!(options & kIOMapReadOnly) || (options & kIOMapCacheMask) != kIOMapInhibitCache ||
        map->getLength() != 0x10000 || !map->getVirtualAddress()) {
        return kIOReturnNotPermitted;
    }
    ConstWindow window(reinterpret_cast<const volatile uint8_t*>(map->getVirtualAddress()), map->getLength(),clientState,&active_);
    gpi0_observer::Sample s{};
    const bool success = gpi0_observer::sample(window, map->getLength(), s);
    result.read_count=window.reads();
    result.resource_phys = 0xe0690000ULL; result.resource_length = 0x10000;
    result.map_options = options; result.revid = s.revid; result.caplist = s.caplist;
    result.padbar = s.padbar; result.stride = s.stride; result.cfg_offset = s.cfgOffset;
    result.cfg0 = s.cfg0; result.cfg1 = s.cfg1; result.padown = s.padOwn;
    result.hostsw = s.hostOwn; result.cfglock = s.cfgLock; result.txlock = s.txLock;
    // Local map destruction precedes return to the user-client copy-out.
    return success ? kIOReturnSuccess : kIOReturnNotReadable;
}

IOReturn G614JVRGPI0Observer::newUserClient(task_t task, void* security, UInt32 type,
                                             OSDictionary* properties, IOUserClient** handler) {
    if (!handler || type != 0 || isInactive() || IOUserClient::clientHasPrivilege(security, kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
        return kIOReturnNotPermitted;
    *handler=nullptr;
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
    const UInt32 previous=OSBitOrAtomic(1,reinterpret_cast<volatile UInt32*>(&state_));
    if (!(previous&2)) finishClosing();
    return kIOReturnSuccess;
}
void G614JVRGPI0UserClient::finishClosing() {
    if (!(OSBitOrAtomic(8,reinterpret_cast<volatile UInt32*>(&state_))&8)) terminate();
}
IOReturn G614JVRGPI0UserClient::clientDied() { return clientClose(); }
IOReturn G614JVRGPI0UserClient::externalMethod(uint32_t selector, IOExternalMethodArguments* args,
                                               IOExternalMethodDispatch*, OSObject*, void*) {
    if (selector != kG0RunObservation || !args || !owner_ || args->version!=kIOExternalMethodArgumentsCurrentVersion ||
        args->asyncWakePort || args->asyncReferenceCount || args->scalarInputCount || args->scalarOutputCount ||
        args->structureInputDescriptor || args->structureOutputDescriptor ||
        args->structureInputSize || args->structureOutputSize != sizeof(GPI0ObservationResult) || !args->structureOutput)
        return kIOReturnBadArgument;
    if (!OSCompareAndSwap(0, 6, &state_)) return kIOReturnBusy;
    retain();
    GPI0ObservationResult result{};
    result.protocol_version=kG0ProtocolVersion;
    const IOReturn status = (OSAddAtomic(0,&state_)&1) ? kIOReturnAborted : owner_->observe(result,&state_);
    result.status=uint32_t(status);
    IOReturn transport=kIOReturnAborted;
    if (!(OSAddAtomic(0, &state_) & 1)) {
        memcpy(args->structureOutput, &result, sizeof(result));
        args->structureOutputSize = sizeof(result);
        transport=kIOReturnSuccess;
    }
    const UInt32 previous=OSBitAndAtomic(~2U,reinterpret_cast<volatile UInt32*>(&state_));
    if (previous&1) finishClosing();
    release();
    return transport;
}
