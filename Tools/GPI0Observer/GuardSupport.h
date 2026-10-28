#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <stdint.h>
#include <mach/mach_time.h>

enum { G0_GUARD_MAX_SECONDS = 30, G0_WORKER_MAX_SECONDS = 20, G0_CLEANUP_GRACE_SECONDS = 5 };
_Static_assert(G0_WORKER_MAX_SECONDS + G0_CLEANUP_GRACE_SECONDS < G0_GUARD_MAX_SECONDS,
               "guard must cover worker deadline plus cleanup grace");
static inline double g0_now(void) {
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    return (double)mach_continuous_time() * (double)info.numer / (double)info.denom / 1.0e9;
}
static inline int g0_ac_connected(void) {
#ifdef G0_GUARD_TEST_PREFLIGHT
    return 1;
#else
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) return 0;
    CFStringRef type = IOPSGetProvidingPowerSourceType(info);
    int yes = type && CFEqual(type, CFSTR(kIOPSACPowerValue));
    CFRelease(info); return yes;
#endif
}
static inline int g0_lid_open(void) {
#ifdef G0_GUARD_TEST_PREFLIGHT
    return 1;
#else
    io_service_t root = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPMrootDomain"));
    if (!root) return 0;
    CFTypeRef state = IORegistryEntryCreateCFProperty(root, CFSTR("AppleClamshellState"), kCFAllocatorDefault, 0);
    int yes = state && CFGetTypeID(state) == CFBooleanGetTypeID() && !CFBooleanGetValue((CFBooleanRef)state);
    if (state) CFRelease(state);
    IOObjectRelease(root); return yes;
#endif
}
typedef struct { uint32_t magic; uint32_t assertion; int32_t supervisor; uint32_t reserved; double worker_deadline; } G0GuardToken;
enum { G0_GUARD_MAGIC = 0x47304744, G0_GUARD_FD = 3 };
static inline int g0_guard_active(int32_t pid) {
    CFDictionaryRef by_pid = NULL;
    if (IOPMCopyAssertionsByProcess(&by_pid) != kIOReturnSuccess || !by_pid) return 0;
    CFNumberRef key = CFNumberCreate(NULL,kCFNumberSInt32Type,&pid);
    CFArrayRef entries = key ? CFDictionaryGetValue(by_pid,key) : NULL;
    int found = 0;
    if (entries && CFGetTypeID(entries)==CFArrayGetTypeID()) {
        for (CFIndex i=0;i<CFArrayGetCount(entries);++i) {
            CFDictionaryRef item=CFArrayGetValueAtIndex(entries,i);
            if (!item || CFGetTypeID(item)!=CFDictionaryGetTypeID()) continue;
            CFTypeRef name=CFDictionaryGetValue(item,kIOPMAssertionNameKey);
            CFTypeRef type=CFDictionaryGetValue(item,kIOPMAssertionTypeKey);
            CFNumberRef level=CFDictionaryGetValue(item,kIOPMAssertionLevelKey); int value=0;
            if (name && CFEqual(name,CFSTR("G614JVR G0 supervisor (30s maximum)")) &&
                type && CFEqual(type,kIOPMAssertionTypePreventUserIdleSystemSleep) &&
                level && CFGetTypeID(level)==CFNumberGetTypeID() &&
                CFNumberGetValue(level,kCFNumberIntType,&value) && value==kIOPMAssertionLevelOn) found=1;
        }
    }
    if (key) CFRelease(key);
    CFRelease(by_pid); return found;
}
