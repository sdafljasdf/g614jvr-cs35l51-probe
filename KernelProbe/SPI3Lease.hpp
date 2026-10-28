// Dedicated G614JVR probe: exclusive SPI3 PCI ownership and BAR mapping.
// No transfer, GPIO, amplifier, reset or firmware operation is implemented here.
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSString.h>
#include <string.h>

namespace g614jvr_probe {
class SPI3Lease final {
public:
    SPI3Lease() = default;
    SPI3Lease(const SPI3Lease&) = delete;
    SPI3Lease& operator=(const SPI3Lease&) = delete;
    ~SPI3Lease() { release(); }

    // The caller must independently validate board and CSC3551 resources.
    IOReturn acquire(IOPCIDevice* pci, IOService* owner) {
        if (!pci || !owner || pci_ || map_) return kIOReturnBadArgument;
        owner->setProperty("SPI3_PROVIDER_FOUND", kOSBooleanTrue);
        owner->setProperty("SPI3_OPEN_ATTEMPTED", kOSBooleanFalse);
        owner->setProperty("SPI3_OPEN_SUCCESS", kOSBooleanFalse);
        owner->setProperty("SPI3_SEIZE_USED", kOSBooleanFalse);
        auto* path = OSDynamicCast(OSString, pci->getProperty("acpi-path"));
        if (!path || !path->isEqualTo("IOACPIPlane:/_SB/PC00@0/SPI3@130000") ||
            pci->configRead16(kIOPCIConfigVendorID) != 0x8086 ||
            pci->configRead16(kIOPCIConfigDeviceID) != 0x7a79)
            return kIOReturnUnsupported;
        const bool alreadyOpen = pci->isOpen();
        owner->setProperty("SPI3_WAS_ALREADY_OPEN", alreadyOpen ? kOSBooleanTrue : kOSBooleanFalse);
        if (alreadyOpen) return kIOReturnExclusiveAccess;
        owner->setProperty("SPI3_OPEN_ATTEMPTED", kOSBooleanTrue);
        // Ordinary arbitration only. No kIOServiceSeize and no pre-open mapping.
        if (!pci->open(owner, 0, nullptr)) return kIOReturnExclusiveAccess;
        pci_ = pci; owner_ = owner; pci_->retain();
        owner->setProperty("SPI3_OPEN_SUCCESS", kOSBooleanTrue);
        // Never change PCI power or command bits. If firmware did not leave
        // the function in D0 with memory decoding enabled, stop safely.
        uint8_t pmOffset = 0;
        (void)pci_->findPCICapability(kIOPCIPowerManagementCapability, &pmOffset);
        const uint16_t command = pci_->configRead16(kIOPCIConfigCommand);
        if (!pmOffset || pmOffset > 0xf8 || command == 0xffff || !(command & 2U) ||
            (pci_->configRead16(uint8_t(pmOffset + 4)) & 3U) != 0) {
            release();
            return kIOReturnNotReady;
        }
        map_ = pci_->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
        if (!map_ || map_->getLength() < 0x300 || !map_->getVirtualAddress()) {
            release();
            return kIOReturnNotReady;
        }
        return kIOReturnSuccess;
    }
    volatile uint8_t* address() const {
        return map_ ? reinterpret_cast<volatile uint8_t*>(map_->getVirtualAddress()) : nullptr;
    }
    bool owned() const { return pci_ && map_; }
    size_t length() const { return map_ ? map_->getLength() : 0; }
    void release() {
        if (map_) { map_->release(); map_ = nullptr; }
        if (pci_) { pci_->close(owner_); owner_->setProperty("SPI3_CLOSE_DONE", kOSBooleanTrue); pci_->release(); pci_ = nullptr; owner_ = nullptr; }
    }
private:
    IOPCIDevice* pci_ = nullptr;
    IOService* owner_ = nullptr;
    IOMemoryMap* map_ = nullptr;
};
}
