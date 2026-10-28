/* One-shot client draft. The kernel service currently exports no matching
 * protocol and must remain passive until lifecycle review is complete. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t resource_phys, resource_length;
    uint32_t map_options, revid, caplist, padbar, stride, cfg_offset;
    uint32_t cfg0, cfg1, padown, hostsw, cfglock, txlock;
} Result;

static int ac_connected(void) {
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) return 0;
    CFStringRef type = IOPSGetProvidingPowerSourceType(info);
    int yes = type && CFEqual(type, CFSTR(kIOPSACPowerValue));
    CFRelease(info);
    return yes;
}
static int lid_open(void) {
    io_registry_entry_t root = IORegistryEntryFromPath(kIOMainPortDefault,
        "IOPower:/IOPowerConnection/IOPMrootDomain");
    if (!root) root = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPMrootDomain"));
    if (!root) return 0;
    CFTypeRef state = IORegistryEntryCreateCFProperty(root, CFSTR("AppleClamshellState"), kCFAllocatorDefault, 0);
    int yes = state && CFGetTypeID(state) == CFBooleanGetTypeID() && !CFBooleanGetValue((CFBooleanRef)state);
    if (state) CFRelease(state);
    IOObjectRelease(root);
    return yes;
}
int main(void) {
    if (!ac_connected() || !lid_open()) { fprintf(stderr, "AC or lid status unavailable or unsafe; stopped\n"); return 1; }
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("G614JVRGPI0Observer"));
    if (!svc) { fprintf(stderr, "Observer service unavailable; stopped\n"); return 1; }
    CFTypeRef protocol = IORegistryEntryCreateCFProperty(svc, CFSTR("G0_TRIGGER_PROTOCOL"), kCFAllocatorDefault, 0);
    const int ready = protocol && CFGetTypeID(protocol) == CFStringGetTypeID() &&
                      CFEqual(protocol, CFSTR("READY_G0_V1"));
    if (protocol) CFRelease(protocol);
    if (!ready) { fprintf(stderr, "Observer trigger is not enabled; stopped\n"); IOObjectRelease(svc); return 1; }
    IOPMAssertionID assertion = kIOPMNullAssertionID;
    IOReturn rc = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep,
        kIOPMAssertionLevelOn, CFSTR("G614JVR one-shot GPI0 observation"), &assertion);
    if (rc != kIOReturnSuccess) { fprintf(stderr, "Sleep assertion failed; stopped\n"); IOObjectRelease(svc); return 1; }
    io_connect_t connection = IO_OBJECT_NULL;
    rc = IOServiceOpen(svc, mach_task_self(), 0, &connection);
    IOObjectRelease(svc);
    if (rc == kIOReturnSuccess) {
        Result result = {0}; size_t size = sizeof(result);
        rc = IOConnectCallStructMethod(connection, 0, NULL, 0, &result, &size);
        if (rc == kIOReturnSuccess && size == sizeof(result)) {
            printf("RESOURCE4_PHYS=0x%llx\nRESOURCE4_LENGTH=%llu\nMAP_OPTIONS=0x%x\n",
                   (unsigned long long)result.resource_phys, (unsigned long long)result.resource_length, result.map_options);
            printf("REVID=0x%08x\nCAPLIST=0x%08x\nPADBAR=0x%x\nPAD_STRIDE=%u\nPADCFG_BASE_OFFSET=0x%x\n",
                   result.revid, result.caplist, result.padbar, result.stride, result.cfg_offset);
            printf("PADCFG0_RAW=0x%08x\nPADCFG1_RAW=0x%08x\nPAD_OWN_RAW=0x%08x\nHOSTSW_OWN_RAW=0x%08x\nPAD_LOCK_RAW=0x%08x\nPAD_TX_LOCK_RAW=0x%08x\n",
                   result.cfg0, result.cfg1, result.padown, result.hostsw, result.cfglock, result.txlock);
            puts("CS1_DESELECTED_NOW=UNKNOWN");
        } else if (rc == kIOReturnSuccess) rc = kIOReturnBadArgument;
        IOServiceClose(connection);
    }
    IOPMAssertionRelease(assertion);
    if (rc != kIOReturnSuccess) fprintf(stderr, "Observer failed: 0x%x\n", rc);
    return rc == kIOReturnSuccess ? 0 : 1;
}
