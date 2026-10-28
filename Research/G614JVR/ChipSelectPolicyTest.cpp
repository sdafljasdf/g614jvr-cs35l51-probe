#include "ChipSelectPolicy.hpp"
#include <cassert>
int main() {
 using namespace g614jvr_probe;
 Evidence e{0x10431b93,true,true,true,true,true,true,true,true,0x1d4,0x140,0xc0};
 auto roles=classify(e);
 assert(roles.accepted && roles.cs0==GpioRole::NativeCS && roles.cs1==GpioRole::ExternalCS);
 assert(roles.gpio0==GpioRole::ExternalCS && roles.gpio1==GpioRole::AmpReset);
 assert(roles.gpio2==GpioRole::SpeakerID && roles.gpio3==GpioRole::Unknown);
 assert(permitsTransition(GpioRole::ExternalCS,true));
 assert(!permitsTransition(GpioRole::ExternalCS,false));
 assert(!permitsTransition(GpioRole::AmpReset,true));
 assert(!permitsTransition(GpioRole::SpeakerID,true));
 assert(!permitsTransition(GpioRole::Interrupt,true));
 assert(!permitsTransition(GpioRole::Unknown,true));
 e.gpio0Pin=0x140; assert(!classify(e).accepted);
 e.gpio0Pin=0x1d4; e.noDeviceProperties=false; assert(!classify(e).accepted);
 e.noDeviceProperties=true; e.subsystem=0x10431c9f; assert(!classify(e).accepted);
}
