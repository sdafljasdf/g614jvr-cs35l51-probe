// Exact-board ACPI validation. Evaluates identification and resource objects;
// no SPI, GPIO, reset, firmware or amplifier transactions.
#pragma once
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSData.h>
#include "Platform/ACPI/ACPISPIResources.hpp"
#include "Platform/ACPI/ACPIGPIOResources.hpp"

namespace g614jvr_probe {
inline bool matchesString(IOACPIPlatformDevice* device, const char* method, const char* expected) {
    OSObject* object = nullptr;
    const auto ret = device->evaluateObject(method, &object);
    auto* value = OSDynamicCast(OSString, object);
    const bool match = ret == kIOReturnSuccess && value && value->isEqualTo(expected);
    OSSafeReleaseNULL(object);
    return match;
}
inline IOReturn validatePlatform(IOACPIPlatformDevice* device) {
    if (!device || !device->getName() || strcmp(device->getName(), "SPK1") ||
        !matchesString(device, "_HID", "CSC3551") || !matchesString(device, "_SUB", "10431B93"))
        return kIOReturnUnsupported;
    char path[256]{};
    int length = sizeof(path);
    const auto* plane = IORegistryEntry::getPlane("IOACPIPlane");
    if (!plane || !device->getPath(path, &length, plane) ||
        strcmp(path, "IOACPIPlane:/_SB/PC00@0/SPI3@130000/SPK1"))
        return kIOReturnUnsupported;
    // Linux's board-specific GPIO fallback is only valid when _DSD is absent.
    // A generic error is not proof of absence. Fail closed unless the API
    // explicitly reports not-found; this may make the probe unavailable.
    if (device->validateObject("_DSD") != kIOReturnNotFound) return kIOReturnUnsupported;
    OSObject* object = nullptr;
    const auto status = device->evaluateObject("_CRS", &object);
    auto* data = OSDynamicCast(OSData, object);
    if (status != kIOReturnSuccess || !data) { OSSafeReleaseNULL(object); return kIOReturnNotReady; }
    const auto* bytes = static_cast<const uint8_t*>(data->getBytesNoCopy());
    const auto size = data->getLength();
    cirrus::transport::ACPISPIResources spi{};
    bool i2c = false;
    bool ok = cirrus::transport::parseACPISPIResources(bytes, size, spi, &i2c) == cirrus::transport::ACPISPIResult::Success &&
              !i2c && spi.count == 2;
    for (unsigned i = 0; ok && i < 2; ++i)
        ok = spi.endpoints[i].endpoint.chipSelect == i && spi.endpoints[i].endpoint.clockHz == 4000000 &&
             spi.endpoints[i].endpoint.mode == 0 && !spi.endpoints[i].shared && spi.endpoints[i].sourceIndex == 0 &&
             !strcmp(spi.endpoints[i].source, "\\_SB.PC00.SPI3");
    cirrus::transport::ACPIGPIOResource cs{}, reset{}, speakerId{};
    if (ok) ok = cirrus::transport::parseACPIGPIOResource(bytes, size, 0, 0, cs, true) &&
                 cirrus::transport::parseACPIGPIOResource(bytes, size, 1, 0, reset, true) &&
                 cirrus::transport::parseACPIGPIOResource(bytes, size, 2, 0, speakerId, false) &&
                 cs.pin == 0x1d4 && reset.pin == 0x140 && speakerId.pin == 0xc0 &&
                 cs.pull == 1 && reset.pull == 2 && speakerId.pull == 1 &&
                 !strcmp(cs.source, "\\_SB.GPI0") && !strcmp(reset.source, "\\_SB.GPI0") &&
                 !strcmp(speakerId.source, "\\_SB.GPI0");
    OSSafeReleaseNULL(object);
    return ok ? kIOReturnSuccess : kIOReturnUnsupported;
}
}
