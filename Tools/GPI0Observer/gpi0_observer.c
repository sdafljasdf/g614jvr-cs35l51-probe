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
#include <signal.h>
#include "KernelGPI0Observer/GPI0Protocol.h"
static volatile sig_atomic_t interrupted = 0;
static void on_interrupt(int signo) { (void)signo; interrupted = 1; }

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
    struct sigaction action = {0}; action.sa_handler = on_interrupt; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0) { fprintf(stderr, "Cannot install interrupt handler; stopped\n"); return 1; }
    IOPMAssertionID assertion = kIOPMNullAssertionID;
    IOReturn rc = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep,
        kIOPMAssertionLevelOn, CFSTR("G614JVR one-shot GPI0 observation"), &assertion);
    if (rc != kIOReturnSuccess) { fprintf(stderr, "Sleep assertion failed; stopped\n"); return 1; }
    fprintf(stderr, "DO NOT CLOSE LID OR MANUALLY SLEEP DURING OBSERVATION\n");
    if (interrupted || !ac_connected() || !lid_open()) {
        fprintf(stderr, "AC or lid status unavailable or unsafe; stopped\n"); IOPMAssertionRelease(assertion); return 1;
    }
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("G614JVRGPI0Observer"));
    if (!svc) { fprintf(stderr, "Observer service unavailable; stopped\n"); IOPMAssertionRelease(assertion); return 1; }
    CFTypeRef protocol = IORegistryEntryCreateCFProperty(svc, CFSTR("G0_TRIGGER_PROTOCOL"), kCFAllocatorDefault, 0);
    const int ready = protocol && CFGetTypeID(protocol) == CFStringGetTypeID() &&
                      CFEqual(protocol, CFSTR("READY_G0_V1"));
    if (protocol) CFRelease(protocol);
    if (!ready || interrupted) { fprintf(stderr, "Observer trigger is not enabled; stopped\n"); IOObjectRelease(svc); IOPMAssertionRelease(assertion); return 1; }
    io_connect_t connection = IO_OBJECT_NULL;
    rc = IOServiceOpen(svc, mach_task_self(), 0, &connection);
    IOObjectRelease(svc);
    if (rc == kIOReturnSuccess) {
        GPI0ObservationResult result = {0}; size_t size = sizeof(result);
        rc = interrupted ? kIOReturnAborted : IOConnectCallStructMethod(connection, kG0RunObservation, NULL, 0, &result, &size);
        if (rc == kIOReturnSuccess && size == sizeof(result)) {
            printf("RESOURCE4_PHYS=0x%llx\nRESOURCE4_LENGTH=%llu\nMAP_OPTIONS=0x%x\n",
                   (unsigned long long)result.resource_phys, (unsigned long long)result.resource_length, result.map_options);
            printf("REVID=0x%08x\nCAPLIST=0x%08x\nPADBAR=0x%x\nPAD_STRIDE=%u\nPADCFG_BASE_OFFSET=0x%x\n",
                   result.revid, result.caplist, result.padbar, result.stride, result.cfg_offset);
            printf("PADCFG0_RAW=0x%08x\nPADCFG1_RAW=0x%08x\nPAD_OWN_RAW=0x%08x\nHOSTSW_OWN_RAW=0x%08x\nPAD_LOCK_RAW=0x%08x\nPAD_TX_LOCK_RAW=0x%08x\n",
                   result.cfg0, result.cfg1, result.padown, result.hostsw, result.cfglock, result.txlock);
            const unsigned mode = (result.cfg0 >> 10) & 15U;
            const unsigned tx_enabled = !(result.cfg0 & (1U << 8));
            const unsigned rx_enabled = !(result.cfg0 & (1U << 9));
            const unsigned rev = result.revid >> 16;
            const unsigned own = rev >= 0x110 ? (result.padown & 7U) : ((result.padown >> 16) & 15U);
            printf("PAD_MODE=%u\nTX_ENABLED=%u\nRX_ENABLED=%u\nPAD_TX_STATE=%u\n",
                   mode, tx_enabled, rx_enabled, result.cfg0 & 1U);
            if (rx_enabled) printf("PAD_RX_STATE=%u\n", (result.cfg0 >> 1) & 1U);
            else puts("PAD_RX_STATE=UNKNOWN_RX_DISABLED");
            printf("PAD_OWN=%u\nHOSTSW_OWN=%u\nPAD_LOCK=%u\nPAD_TX_LOCK=%u\n",
                   own, (result.hostsw >> 20) & 1U, (result.cfglock >> 20) & 1U,
                   (result.txlock >> 20) & 1U);
            puts("CS1_DESELECTED_NOW=UNKNOWN");
        } else if (rc == kIOReturnSuccess) rc = kIOReturnBadArgument;
        IOServiceClose(connection);
    }
    IOPMAssertionRelease(assertion);
    if (rc != kIOReturnSuccess) fprintf(stderr, "Observer failed: 0x%x\n", rc);
    return rc == kIOReturnSuccess ? 0 : 1;
}
