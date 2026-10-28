// Dedicated one-shot service. Never instantiates CirrusAudioFixup, an amp
// driver, a firmware loader, an audio event callback, or a retry timer.
#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "KernelProbe/PlatformValidation.hpp"
#include "KernelProbe/SPI3Lease.hpp"
#include "KernelProbe/GPIOProviderLease.hpp"
#include "KernelProbe/GPIOWindow.hpp"
#include "KernelProbe/IdentityOnlySPI.hpp"

class G614JVRProbe final : public IOService {
    OSDeclareDefaultStructors(G614JVRProbe)
    using super = IOService;
public:
    bool start(IOService* provider) override;
private:
    IOReturn probeOnce(IOService* provider);
};
OSDefineMetaClassAndStructors(G614JVRProbe, IOService)
extern "C" __attribute__((visibility("default"))) kern_return_t G614JVRProbe_start(kmod_info_t*, void*) { return KERN_SUCCESS; }
extern "C" __attribute__((visibility("default"))) kern_return_t G614JVRProbe_stop(kmod_info_t*, void*) { return KERN_SUCCESS; }

namespace {
volatile SInt32 gProbeAttempted = 0;
class EngineCleanup final {
public:
    explicit EngineCleanup(cirrus::transport::intel::LPSSSPIEngine& engine) : engine_(engine) {}
    ~EngineCleanup() { finish(); }
    void finish() { if (initialized_) { engine_.shutdown(); initialized_ = false; } }
    bool initialized_ = false;
private:
    cirrus::transport::intel::LPSSSPIEngine& engine_;
};
}

bool G614JVRProbe::start(IOService* provider) {
    if (!super::start(provider)) return false;
    setProperty("SPI3_SEIZE_USED", kOSBooleanFalse);
    setProperty("SPI3_PROVIDER_FOUND", kOSBooleanFalse);
    setProperty("SPI3_OPEN_ATTEMPTED", kOSBooleanFalse);
    setProperty("SPI3_OPEN_SUCCESS", kOSBooleanFalse);
    setProperty("SPI3_CLOSE_DONE", kOSBooleanFalse);
    setProperty("SPI_CONTROLLER_MMIO_WRITES", uint64_t(0), 64);
    setProperty("CS_GPIO_WRITES", uint64_t(0), 64);
    setProperty("CS_READ_COUNT", uint64_t(0), 32);
    setProperty("AMP_REGISTER_WRITES", uint64_t(0), 32);
    setProperty("RESET_GPIO_WRITES", uint64_t(0), 32);
    setProperty("SPKR_ID_GPIO_WRITES", uint64_t(0), 32);
    setProperty("UNKNOWN_GPIO_WRITES", uint64_t(0), 32);
    setProperty("SPI_CONTROLLER_RESET_WRITES", uint64_t(0), 32);
    if (!OSCompareAndSwap(0, 1, &gProbeAttempted)) {
        setProperty("PROBE_RESULT", "ALREADY_ATTEMPTED");
        registerService();
        return true;
    }
    const IOReturn result = probeOnce(provider);
    setProperty("PROBE_RESULT", uint64_t(uint32_t(result)), 32);
    // Publish results; no callbacks, pending timers, or second attempts.
    registerService();
    return true;
}

IOReturn G614JVRProbe::probeOnce(IOService* provider) {
    auto* acpi = OSDynamicCast(IOACPIPlatformDevice, provider);
    auto ret = g614jvr_probe::validatePlatform(acpi);
    if (ret != kIOReturnSuccess) return ret;
    auto* pciEntry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PC00@0/AppleACPIPCI/SPI3@13");
    auto* pci = OSDynamicCast(IOPCIDevice, pciEntry);
    g614jvr_probe::SPI3Lease spi;
    ret = spi.acquire(pci, this);
    OSSafeReleaseNULL(pciEntry);
    if (ret != kIOReturnSuccess) return ret; // no BAR or GPIO write on failed open
    g614jvr_probe::ProbeMMIO mmio(spi.address(), spi.length(), this);
    cirrus::transport::intel::LPSSSPIProfile profile{};
    if (!cirrus::transport::intel::lookupLPSSSPIProfile(0x8086, 0x7a79, profile)) return kIOReturnUnsupported;
    cirrus::transport::intel::LPSSSPIEngine engine(mmio, false); // controller reset prohibited
    EngineCleanup engineCleanup(engine);
    ret = engine.initialize(profile);
    if (ret != kIOReturnSuccess) return ret;
    engineCleanup.initialized_ = true;
    auto* gpioEntry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/GPI0");
    auto* gpio = OSDynamicCast(IOACPIPlatformDevice, gpioEntry);
    g614jvr_probe::GPIOProviderLease gpioLease;
    ret = gpioLease.acquire(gpio, this, spi, 4);
    OSSafeReleaseNULL(gpioEntry);
    if (ret != kIOReturnSuccess) return ret;
    g614jvr_probe::GPIOWindow gpioWindow(gpioLease, this);
    if (!gpioWindow.prepare()) return kIOReturnNotReady;
    g614jvr_probe::WhitelistedCS cs;
    ret = cs.bind(gpioWindow, gpioLease.length(), "\\_SB.GPI0", "INTC1085", 0x01d4);
    if (ret != kIOReturnSuccess) return ret;
    g614jvr_probe::IdentityOnlySPI probe(engine, cs, mmio);
    const auto result = probe.run();
    cs.unbind();
    engineCleanup.finish();
    setProperty("CS_READ_COUNT", uint64_t(result.reads), 32);
    setProperty("CS_TRANSACTION_ATTEMPTS", uint64_t(result.attempted), 32);
    for (unsigned i = 0; i < result.attempted; ++i) {
        const char* keys[] = {"CS0_DEVID_RETURN", "CS0_REVID_RETURN", "CS1_DEVID_RETURN", "CS1_REVID_RETURN"};
        setProperty(keys[i], uint64_t(uint32_t(result.transfer[i])), 32);
    }
    if (result.completed > 0) setProperty("CS0_DEVID", uint64_t(result.device[0]), 32);
    if (result.completed > 1) setProperty("CS0_REVID", uint64_t(result.revision[0]), 32);
    if (result.completed > 2) setProperty("CS1_DEVID", uint64_t(result.device[1]), 32);
    if (result.completed > 3) setProperty("CS1_REVID", uint64_t(result.revision[1]), 32);
    setProperty("CS_GPIO_WRITES", gpioWindow.writes(), 64);
    setProperty("SPI_CONTROLLER_MMIO_WRITES", mmio.writes(), 64);
    if (gpioWindow.denied() || mmio.denied()) return kIOReturnNotPermitted;
    return result.error;
}
