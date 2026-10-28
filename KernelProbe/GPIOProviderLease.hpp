// GPIO provider ownership only. No GPIO read/write or pad configuration.
#pragma once
#include "KernelProbe/SPI3Lease.hpp"
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <libkern/c++/OSData.h>

namespace g614jvr_probe {
class GPIOProviderLease final {
public:
    GPIOProviderLease() = default;
    GPIOProviderLease(const GPIOProviderLease&) = delete;
    GPIOProviderLease& operator=(const GPIOProviderLease&) = delete;
    ~GPIOProviderLease() { release(); }
    // Controller must already be exclusively opened. All platform and ACPI
    // GPIO descriptor checks must already have passed independently.
    IOReturn acquire(IOACPIPlatformDevice* gpio, IOService* owner, const SPI3Lease& controller, uint32_t memoryIndex) {
        if (!gpio || !owner || gpio_ || map_ || !controller.owned() || memoryIndex != 4)
            return kIOReturnBadArgument;
        auto* hid = OSDynamicCast(OSData, gpio->getProperty("name"));
        if (!gpio->getName() || strcmp(gpio->getName(), "GPI0") || !hid || hid->getLength() < 8 ||
            hid->getLength() > 9 || memcmp(hid->getBytesNoCopy(), "INTC1085", 8))
            return kIOReturnUnsupported;
        if (gpio->isOpen() || !gpio->open(owner, 0, nullptr)) return kIOReturnExclusiveAccess;
        gpio_ = gpio; owner_ = owner; gpio_->retain();
        map_ = gpio_->mapDeviceMemoryWithIndex(memoryIndex);
        if (!map_ || !map_->getVirtualAddress() || map_->getLength() < 0x200) {
            release();
            return kIOReturnNotReady;
        }
        return kIOReturnSuccess;
    }
    volatile uint8_t* address() const { return map_ ? reinterpret_cast<volatile uint8_t*>(map_->getVirtualAddress()) : nullptr; }
    size_t length() const { return map_ ? map_->getLength() : 0; }
    void release() {
        if (map_) { map_->release(); map_ = nullptr; }
        if (gpio_) { gpio_->close(owner_); gpio_->release(); gpio_ = nullptr; owner_ = nullptr; }
    }
private:
    IOACPIPlatformDevice* gpio_ = nullptr;
    IOService* owner_ = nullptr;
    IOMemoryMap* map_ = nullptr;
};
}
