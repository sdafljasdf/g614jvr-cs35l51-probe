"""Deterministic tests for the actual PowerGate and SPI3Lease headers.

Simulated IOKit establishes call ordering only; it cannot validate macOS PM
scheduling, electrical CS state, or real MMIO safety.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='g614-pm-test-') as name:
    root = Path(name)
    for folder in ['IOKit/pci', 'IOKit/pwr_mgt', 'libkern/c++', 'kern']:
        (root / folder).mkdir(parents=True, exist_ok=True)
    (root/'IOKit/IOService.h').write_text(r'''#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
using SInt32=int32_t; using IOPMPowerFlags=uint64_t; using IOReturn=int;
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=-1,kIOReturnUnsupported=-2,
 kIOReturnExclusiveAccess=-3,kIOReturnNotReady=-4,IOPMAckImplied=0;
constexpr IOPMPowerFlags kIOPMDeviceUsable=0x8000;
constexpr unsigned kIOPMSuperclassPolicy1=1;
struct OSObject {virtual ~OSObject()=default;};
struct OSString: OSObject {const char* value; explicit OSString(const char* x):value(x){} bool isEqualTo(const char* x){return !std::strcmp(value,x);}};
#define OSDynamicCast(T,x) dynamic_cast<T*>(x)
inline OSObject yes, no; inline OSObject* kOSBooleanTrue=&yes; inline OSObject* kOSBooleanFalse=&no;
struct IOService: OSObject {
 IOPMPowerFlags flags=kIOPMDeviceUsable; unsigned long ordinal=7; std::atomic_bool tickle{true};
 int interests=0, deregisters=0, usableRequests=0; virtual ~IOService()=default;
 virtual IOPMPowerFlags registerInterestedDriver(IOService*){++interests;return flags;}
 virtual IOReturn deRegisterInterestedDriver(IOService*){++deregisters;return 0;}
 virtual IOReturn makeUsable(){++usableRequests;return 0;}
 virtual IOPMPowerFlags currentCapability(){return flags;}
 virtual unsigned long getPowerState(){return ordinal;}
 virtual bool activityTickle(unsigned,unsigned long){return tickle.load();}
 void setProperty(const char*,OSObject*){} void setProperty(const char*,uint64_t,unsigned){}
};
''')
    (root/'libkern/c++/OSString.h').write_text('#pragma once\n#include <IOKit/IOService.h>\n')
    (root/'IOKit/pwr_mgt/IOPM.h').write_text('#pragma once\n#include <IOKit/IOService.h>\n')
    (root/'libkern/OSAtomic.h').write_text(r'''#pragma once
#include <cstdint>
inline bool OSCompareAndSwap(int32_t a,int32_t b,volatile int32_t* p){return __sync_bool_compare_and_swap(p,a,b);}
inline int32_t OSIncrementAtomic(volatile int32_t* p){return __sync_fetch_and_add(p,1);}
inline int32_t OSAddAtomic(int32_t v,volatile int32_t* p){return __sync_fetch_and_add(p,v);}
''')
    (root/'kern/kern_types.h').write_text('#pragma once\nconstexpr int THREAD_ABORTSAFE=2, THREAD_AWAKENED=0, THREAD_TIMED_OUT=1;\n')
    (root/'kern/clock.h').write_text(r'''#pragma once
#include <cstdint>
#include <chrono>
inline void clock_interval_to_deadline(uint32_t,uint32_t,uint64_t* d){*d=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()+30000000);}
''')
    (root/'IOKit/IOLocks.h').write_text(r'''#pragma once
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdint>
struct IOLock {std::mutex m;std::condition_variable cv;};
inline IOLock* IOLockAlloc(){return new IOLock;} inline void IOLockFree(IOLock* l){delete l;}
inline void IOLockLock(IOLock* l){l->m.lock();} inline void IOLockUnlock(IOLock* l){l->m.unlock();}
inline void IOLockWakeup(IOLock* l,void*,bool){l->cv.notify_all();}
inline int IOLockSleepDeadline(IOLock* l,void*,uint64_t deadline,unsigned){
 std::unique_lock<std::mutex> lock(l->m,std::adopt_lock);
 auto time=std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline));
 auto result=l->cv.wait_until(lock,time);lock.release();return result==std::cv_status::timeout?1:0;
}
''')
    (root/'IOKit/pci/IOPCIDevice.h').write_text(r'''#pragma once
#include <IOKit/IOService.h>
#include <vector>
constexpr unsigned kIOPCIConfigVendorID=0,kIOPCIConfigDeviceID=2,kIOPCIConfigCommand=4,
 kIOPCIConfigBaseAddress0=0x10,kIOPCIPowerManagementCapability=1;
struct IOMemoryMap {size_t getLength(){return 4096;} uintptr_t getVirtualAddress(){return 1;} void release(){}};
struct IOPCIDevice: IOService {
 OSString path{"IOACPIPlane:/_SB/PC00@0/SPI3@130000"}; bool allowOpen=true, opened=false; int maps=0, closes=0;
 OSObject* getProperty(const char*){return &path;}
 uint16_t configRead16(unsigned x){return x==0?0x8086:x==2?0x7a79:x==4?2:0;}
 bool isOpen(){return opened;} bool open(IOService*,int,void*){if(!allowOpen)return false;opened=true;return true;}
 void retain(){} void release(){} void close(IOService*){opened=false;++closes;}
 unsigned findPCICapability(unsigned,uint8_t* p){*p=0x80;return 1;}
 IOMemoryMap* mapDeviceMemoryWithRegister(unsigned){++maps;static IOMemoryMap map;return &map;}
};
''')
    (root/'test.cpp').write_text(r'''#include <cassert>
#include <thread>
#include <atomic>
#include <chrono>
#include "KernelProbe/PowerGate.hpp"
#include "KernelProbe/SPI3Lease.hpp"
using namespace g614jvr_probe;
using namespace std::chrono_literals;
int main(){
 // 1: failed arbitration cannot map BAR or touch GPIO/amp.
 {IOService owner;IOPCIDevice pci;pci.allowOpen=false;SPI3Lease lease;
  assert(lease.acquire(&pci,&owner)==kIOReturnExclusiveAccess);assert(pci.maps==0&&!pci.opened);}
 // 2: pending makeUsable does not grant hardware access.
 {IOService owner,p;p.flags=2;PowerGate gate(&owner);assert(gate.watchSPI(&p)==kIOReturnNotReady);assert(!gate.begin(false));}
 // 3: a usable notification permits the gated section.
 {IOService owner,p;p.flags=2;PowerGate gate(&owner);std::thread callback([&]{std::this_thread::sleep_for(5ms);gate.did(&p,kIOPMDeviceUsable,7);});
  assert(gate.watchSPI(&p)==0);callback.join();assert(gate.begin(false));gate.end();}
 // 4: a false tickle waits for a usable notification; no section starts early.
 {IOService owner,p;PowerGate gate(&owner);assert(gate.watchSPI(&p)==0);p.tickle=false;
  std::thread callback([&]{std::this_thread::sleep_for(5ms);p.tickle=true;gate.did(&p,kIOPMDeviceUsable,7);});
  assert(gate.begin(false,true));gate.end();callback.join();}
 // 5: power down before entry prevents any new section.
 {IOService owner,p;PowerGate gate(&owner);assert(gate.watchSPI(&p)==0);gate.will(&p);assert(!gate.begin(false));}
 // 6: power down after one operation blocks all subsequent sections.
 {IOService owner,p;PowerGate gate(&owner);assert(gate.watchSPI(&p)==0);assert(gate.begin(false));gate.end();gate.will(&p);assert(!gate.begin(false));}
 // 7: a will callback cannot acknowledge until the current gated section ends.
 {IOService owner,p;PowerGate gate(&owner);assert(gate.watchSPI(&p)==0);assert(gate.begin(false));
  std::atomic<bool> entered{false},done{false};std::thread callback([&]{entered=true;gate.will(&p);done=true;});
  while(!entered)std::this_thread::yield();std::this_thread::sleep_for(5ms);assert(!done);gate.end();callback.join();assert(done&&!gate.begin(false));}
 // Known limitation: the implementation has no independent hard deadline on
 // callback lock acquisition. This deliberately holds the section longer to
 // demonstrate that the callback remains blocked, not to certify boundedness.
 {IOService owner,p;PowerGate gate(&owner);assert(gate.watchSPI(&p)==0);assert(gate.begin(false));
  std::atomic<bool> entered{false},done{false};std::thread callback([&]{entered=true;gate.will(&p);done=true;});
  while(!entered)std::this_thread::yield();std::this_thread::sleep_for(60ms);assert(!done);gate.end();callback.join();}
 // 8: interests are deregistered before the exclusive provider is closed.
 {IOService owner;IOPCIDevice pci;SPI3Lease lease;assert(lease.acquire(&pci,&owner)==0);
  {PowerGate gate(&owner);assert(gate.watchSPI(&pci)==0);gate.close();assert(pci.deregisters==1&&pci.closes==0);}
  lease.release();assert(pci.closes==1&&pci.maps==0);}
}
''')
    exe = root/'probe-pm-test'
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',
                    '-I'+str(root),'-I'+str(ROOT),str(root/'test.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)
print('PASS eight simulated lifecycle cases; LIMITATION callback lock wait has no hard deadline; hardware untested')
