"""Compile the real observer/user-client source against host IOKit fakes.
Tests admission, map lifetime, bad arguments, close/death and concurrency.
No kernel module is loaded and the reads use an allocated synthetic array.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='g0-userclient-test-') as directory:
    p=Path(directory)
    for folder in ('IOKit/acpi','libkern/c++'):(p/folder).mkdir(parents=True)
    (p/'IOKit/IOService.h').write_text(r'''#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <map>
using SInt32=int32_t;using UInt32=uint32_t;using UInt64=uint64_t;using IOOptionBits=uint32_t;
using IOReturn=int;using task_t=void*;using kern_return_t=int;struct kmod_info_t{};
constexpr int KERN_SUCCESS=0,kIOReturnSuccess=0,kIOReturnBusy=-1,kIOReturnNotReady=-2,kIOReturnUnsupported=-3,
 kIOReturnExclusiveAccess=-4,kIOReturnAborted=-5,kIOReturnNoMemory=-6,kIOReturnNotPermitted=-7,
 kIOReturnNotReadable=-8,kIOReturnError=-9,kIOReturnBadArgument=-10;
constexpr unsigned kIOMapReadOnly=0x1000,kIOMapInhibitCache=0x100,kIOMapCacheMask=0xf00;
struct OSObject {std::atomic<int> refs{1};virtual ~OSObject()=default;virtual void free(){}
 void retain(){++refs;}void release(){if(--refs==0){free();delete this;}}};
struct OSDictionary:OSObject{};
struct OSData:OSObject {const char* bytes="INTC1085";unsigned getLength(){return 8;}const void* getBytesNoCopy(){return bytes;}};
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
#define OSSafeReleaseNULL(p) do{if(p){p->release();p=nullptr;}}while(0)
#define OSDeclareDefaultStructors(T) public:T()=default;~T() override=default;
#define OSDefineMetaClassAndStructors(T,B)
struct IOUserClient;
inline OSObject* node=nullptr;inline std::array<uint32_t,0x4000> image{};
inline int mapAlive=0,mapCalls=0;inline unsigned returnedOptions=kIOMapReadOnly|kIOMapInhibitCache;
inline uint64_t physical=0xe0690000;inline std::function<void()> mapHook;
struct IOMemoryMap:OSObject {IOMemoryMap(){++mapAlive;}~IOMemoryMap(){--mapAlive;}
 unsigned getMapOptions(){return returnedOptions;}uint64_t getLength(){return 0x10000;}
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(image.data());}};
struct IODeviceMemory:OSObject {uint64_t getLength(){return 0x10000;}uint64_t getPhysicalAddress(){return physical;}};
struct IORegistryEntry:OSObject {
 static IORegistryEntry* fromPath(const char*) {auto*p=dynamic_cast<IORegistryEntry*>(node);if(p)p->retain();return p;}
 virtual OSObject* getProperty(const char*){return nullptr;}
};
struct IOService:IORegistryEntry {
 IOService* provider=nullptr;bool opened=false,inactive=false,terminated=false;OSData hid;IODeviceMemory mem;
 std::map<std::string,std::string> properties;
 virtual bool start(IOService*p){provider=p;return true;}virtual void stop(IOService*){};
 void free() override{};bool isInactive(){return inactive;}virtual bool attach(IOService*p){provider=p;return true;}
 virtual void detach(IOService*){provider=nullptr;}virtual bool terminate(){terminated=true;return true;}
 virtual IOReturn newUserClient(task_t,void*,UInt32,OSDictionary*,IOUserClient**){return -1;}
 void setProperty(const char*k,const char*v){properties[k]=v;}void setProperty(const char*,uint64_t,unsigned){}
 void registerService(){};OSObject* getProperty(const char*) override{return &hid;}
 unsigned getDeviceMemoryCount(){return 5;}IODeviceMemory* getDeviceMemoryWithIndex(unsigned i){return i==4?&mem:nullptr;}
 bool isOpen(){return opened;}bool open(IOService*,int,void*){if(opened)return false;opened=true;return true;}
 void close(IOService*){opened=false;}
 IOMemoryMap* mapDeviceMemoryWithIndex(unsigned i,unsigned){if(i!=4)return nullptr;++mapCalls;auto*m=new IOMemoryMap;if(mapHook)mapHook();return m;}
};
struct IOACPIPlatformDevice:IOService{};
inline void OSSynchronizeIO(){}
''')
    (p/'IOKit/IOMemoryDescriptor.h').write_text('#pragma once\n#include <IOKit/IOService.h>\n')
    (p/'IOKit/acpi/IOACPIPlatformDevice.h').write_text('#pragma once\n#include <IOKit/IOService.h>\n')
    (p/'libkern/c++/OSData.h').write_text('#pragma once\n#include <IOKit/IOService.h>\n')
    (p/'libkern/OSAtomic.h').write_text(r'''#pragma once
#include <IOKit/IOService.h>
inline bool OSCompareAndSwap(uint32_t a,uint32_t b,volatile SInt32*p){return __sync_bool_compare_and_swap(p,a,b);}
inline SInt32 OSAddAtomic(SInt32 v,volatile SInt32*p){return __sync_fetch_and_add(p,v);}
inline uint32_t OSBitOrAtomic(uint32_t v,volatile uint32_t*p){return __sync_fetch_and_or(p,v);}
inline uint32_t OSBitAndAtomic(uint32_t v,volatile uint32_t*p){return __sync_fetch_and_and(p,v);}
''')
    (p/'IOKit/IOUserClient.h').write_text(r'''#pragma once
#include <IOKit/IOService.h>
constexpr unsigned kIOExternalMethodArgumentsCurrentVersion=2;
constexpr const char*kIOClientPrivilegeAdministrator="root";
struct IOExternalMethodArguments {uint32_t version=2,asyncWakePort=0,asyncReferenceCount=0,
 scalarInputCount=0,scalarOutputCount=0,structureInputSize=0,structureOutputSize=0;
 void*structureInputDescriptor=nullptr;void*structureOutputDescriptor=nullptr;void*structureOutput=nullptr;};
struct IOExternalMethodDispatch{};
struct IOUserClient:IOService {
 virtual bool initWithTask(task_t,void*,UInt32,OSDictionary*){return true;}
 static int clientHasPrivilege(void*p,const char*){return p==reinterpret_cast<void*>(1)?0:-7;}
 virtual IOReturn clientClose(){return 0;}virtual IOReturn clientDied(){return 0;}
 virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*){return -1;}
};
''')
    (p/'test.cpp').write_text(r'''
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "KernelGPI0Observer/G614JVRGPI0Observer.cpp"
struct Fixture {
 IOACPIPlatformDevice* p=new IOACPIPlatformDevice;G614JVRGPI0Observer* s=new G614JVRGPI0Observer;
 Fixture(){node=p;image.fill(0);image[0]=0x00940000;image[1]=1;image[3]=0x700;image[0x840/4]=1;
  physical=0xe0690000;returnedOptions=kIOMapReadOnly|kIOMapInhibitCache;mapCalls=0;mapHook=nullptr;
  assert(s->start(p));assert(mapCalls==0);}
 IOUserClient* client(){IOUserClient*c=nullptr;assert(s->newUserClient(nullptr,reinterpret_cast<void*>(1),0,nullptr,&c)==0);return c;}
 ~Fixture(){mapHook=nullptr;assert(mapAlive==0&&!p->opened);s->release();p->release();node=nullptr;}
};
int call(IOUserClient*c,GPI0ObservationResult&r,unsigned selector=0){IOExternalMethodArguments a{};a.structureOutput=&r;a.structureOutputSize=sizeof(r);return c->externalMethod(selector,&a,nullptr,nullptr,nullptr);}
int main(){
 {Fixture f;IOUserClient*c=nullptr;assert(f.s->newUserClient(nullptr,nullptr,0,nullptr,&c)==kIOReturnNotPermitted);assert(mapCalls==0);}
 {Fixture f;auto*c=f.client();GPI0ObservationResult r{};assert(call(c,r,1)==kIOReturnBadArgument&&mapCalls==0);c->clientClose();assert(call(c,r)==kIOReturnBusy);c->release();}
 {Fixture f;auto*c=f.client();GPI0ObservationResult r{};physical=1;assert(call(c,r)==0&&r.status==uint32_t(kIOReturnNotReady)&&mapCalls==0);c->clientClose();c->release();}
 {Fixture f;auto*c=f.client();GPI0ObservationResult r{};returnedOptions=kIOMapInhibitCache;assert(call(c,r)==0&&r.status==uint32_t(kIOReturnNotPermitted)&&r.read_count==0&&mapAlive==0);c->clientClose();c->release();}
 {Fixture f;auto*c=f.client();GPI0ObservationResult r{};assert(call(c,r)==0&&r.status==0&&r.read_count==9&&mapAlive==0&&!f.p->opened);assert(call(c,r)==kIOReturnBusy);c->clientClose();c->release();}
 {Fixture f;auto*c=f.client();GPI0ObservationResult r{};mapHook=[&]{c->clientDied();assert(!c->terminated&&mapAlive==1);};assert(call(c,r)==kIOReturnAborted&&mapAlive==0&&c->terminated);c->release();}
 {Fixture f;auto*c=f.client();auto*d=f.client();GPI0ObservationResult r{},r2{};
  std::mutex m;std::condition_variable cv;bool entered=false,resume=false;
  mapHook=[&]{std::unique_lock<std::mutex> l(m);entered=true;cv.notify_all();cv.wait(l,[&]{return resume;});};
  std::thread worker([&]{assert(call(c,r)==kIOReturnAborted);});
  {std::unique_lock<std::mutex>l(m);cv.wait(l,[&]{return entered;});}
  assert(call(c,r2)==kIOReturnBusy);assert(call(d,r2)==0&&r2.status==uint32_t(kIOReturnBusy)&&mapCalls==1);
  c->clientClose();assert(!c->terminated);
  {std::lock_guard<std::mutex>l(m);resume=true;}cv.notify_all();worker.join();assert(c->terminated&&mapAlive==0);d->clientClose();d->release();c->release();}
}
''')
    exe=p/'test'
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I'+str(p),'-I'+str(ROOT),str(p/'test.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
print('PASS actual user-client source: passive load, root gate, bad selector, resource/map rejection, local cleanup, close/death and concurrent admission')
