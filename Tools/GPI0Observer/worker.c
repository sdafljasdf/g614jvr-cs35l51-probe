/* G0 worker. Requires a live, separate supervisor and its active assertion. */
#include "GuardSupport.h"
#include "KernelGPI0Observer/GPI0Protocol.h"
#include <IOKit/IOMapTypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static volatile sig_atomic_t interrupted;
static void on_interrupt(int sig) { (void)sig; interrupted = 1; }
static void print_result(const GPI0ObservationResult *r) {
    printf("PROTOCOL_VERSION=%u\nOBSERVATION_STATUS=0x%08x\nREAL_GPIO_READ_OCCURRED=%s\nREAD_COUNT=%u\n",
           r->protocol_version,r->status,r->read_count?"YES":"NO",r->read_count);
    printf("RESOURCE4_PHYS=0x%llx\nRESOURCE4_LENGTH=%llu\nMAP_OPTIONS_REQUESTED=0x%x\nMAP_OPTIONS_RETURNED=0x%x\nMAP_READ_ONLY=%s\n",
           (unsigned long long)r->resource_phys,(unsigned long long)r->resource_length,r->map_options_requested,r->map_options,
           (r->map_options & kIOMapReadOnly)?"YES":"NO");
    printf("REVID=0x%08x\nCAPLIST=0x%08x\nPADBAR=0x%x\nCOMMUNITY=5\nPIN=290\nCOMMUNITY_PIN_BASE=270\nPADNO=20\n",
           r->revid,r->caplist,r->padbar);
    if (r->status != 0 || r->read_count != 9) {
        puts("PAD_STATE_VALID=NO\nCS1_DESELECTED_NOW=UNKNOWN\nREAL_SPI_READ_OCCURRED=NO"); return;
    }
    const unsigned mode=(r->cfg0>>10)&15U, tx=!(r->cfg0&(1U<<8)), rx=!(r->cfg0&(1U<<9));
    const unsigned revision=r->revid>>16;
    const unsigned own=revision>=0x110 ? (r->padown&7U) : ((r->padown>>16)&15U);
    const unsigned host=(r->hostsw>>20)&1U, cfglock=(r->cfglock>>20)&1U, txlock=(r->txlock>>20)&1U;
    const unsigned term=(r->cfg1>>10)&7U, up=(r->cfg1>>13)&1U;
    const unsigned ohms[8]={0,1000,5000,833,20000,952,4000,800};
    printf("FEATURE_DEBOUNCE=%s\nNREGS=%u\nPAD_STRIDE=%u\nPADCFG_BASE_OFFSET=0x%x\nPADCFG0_RAW=0x%08x\nPADCFG1_RAW=0x%08x\n",
           r->stride==16?"YES":"NO",r->stride/4,r->stride,r->cfg_offset,r->cfg0,r->cfg1);
    printf("PAD_OWN_RAW=0x%08x\nHOSTSW_OWN_RAW=0x%08x\nPAD_LOCK_RAW=0x%08x\nPAD_TX_LOCK_RAW=0x%08x\nPAD_OWN=%u\nHOSTSW_OWN=%u\n",
           r->padown,r->hostsw,r->cfglock,r->txlock,own,host);
    printf("PAD_MODE=%s\nPAD_MODE_VALUE=%u\nTX_ENABLED=%s\nRX_ENABLED=%s\nPAD_TX_STATE=%s\n",
           mode==0?"GPIO":"NATIVE",mode,tx?"YES":"NO",rx?"YES":"NO",(r->cfg0&1)?"HIGH":"LOW");
    if (rx) printf("PAD_RX_STATE=%s\n",(r->cfg0&2)?"HIGH":"LOW"); else puts("PAD_RX_STATE=UNKNOWN_RX_DISABLED");
    printf("PAD_PULL=%s\nPAD_PULL_OHMS=%u\nPAD_LOCK=%u\nPAD_TX_LOCK=%u\n",
           term?(up?"UP":"DOWN"):"NONE",ohms[term],cfglock,txlock);
    // This is a sampled TX-latch/mode condition, not a power/ownership lease.
    const int inactive=(mode==0 && tx && (r->cfg0&1) && own==0 && host==1);
    printf("CS1_DESELECTED_NOW=%s\nMMIO_WRITES=0\nGPIO_WRITES=0\nREGISTER_SCAN=NO\nREAL_SPI_READ_OCCURRED=NO\n",inactive?"YES":"NO");
}
int main(int argc,char **argv) {
    (void)argv;
    if (argc!=1 || geteuid()!=0) { fprintf(stderr,"Run the supervisor with administrator privileges.\n"); return 2; }
    struct sigaction sa={0}; sa.sa_handler=on_interrupt; sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT,&sa,NULL)||sigaction(SIGTERM,&sa,NULL)) return 2;
    struct pollfd p={G0_GUARD_FD,POLLIN,0};
    if (poll(&p,1,2000)!=1) { fprintf(stderr,"No supervisor pipe; stopped.\n"); return 2; }
    G0GuardToken token={0}; ssize_t received=read(G0_GUARD_FD,&token,sizeof(token)); close(G0_GUARD_FD);
    if (received!=sizeof(token) || token.magic!=G0_GUARD_MAGIC || token.supervisor!=getppid() || token.reserved ||
        g0_now()>=token.worker_deadline || !g0_guard_active(token.supervisor) || !g0_ac_connected() || !g0_lid_open() || interrupted) {
        fprintf(stderr,"Supervisor guard, AC or lid validation failed; no observation.\n"); return 2;
    }
    io_service_t svc=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("G614JVRGPI0Observer"));
    if (!svc) { fprintf(stderr,"Observer service unavailable; stopped.\n"); return 2; }
    CFTypeRef protocol=IORegistryEntryCreateCFProperty(svc,CFSTR("G0_TRIGGER_PROTOCOL"),kCFAllocatorDefault,0);
    int ready=protocol && CFGetTypeID(protocol)==CFStringGetTypeID() && CFEqual(protocol,CFSTR("READY_G0_SUPERVISED_V2"));
    if (protocol) CFRelease(protocol);
    if (!ready) { IOObjectRelease(svc); fprintf(stderr,"Observer protocol mismatch; stopped.\n"); return 2; }
    io_connect_t connection=IO_OBJECT_NULL;
    IOReturn rc=IOServiceOpen(svc,mach_task_self(),0,&connection); IOObjectRelease(svc);
    GPI0ObservationResult result={0}; size_t size=sizeof(result);
    if (rc==kIOReturnSuccess) {
        if (interrupted || getppid()!=token.supervisor || g0_now()>=token.worker_deadline || !g0_guard_active(token.supervisor))
            rc=kIOReturnAborted;
        else rc=IOConnectCallStructMethod(connection,kG0RunObservation,NULL,0,&result,&size);
        IOServiceClose(connection);
    }
    if (rc!=kIOReturnSuccess || size!=sizeof(result) || result.protocol_version!=kG0ProtocolVersion) {
        fprintf(stderr,"Observation transport failed: 0x%x; no retry.\n",rc); return 1;
    }
    print_result(&result); fflush(stdout);
    return result.status==0 && result.read_count==9 ? 0 : 1;
}
