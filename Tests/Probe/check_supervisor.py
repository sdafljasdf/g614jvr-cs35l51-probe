"""Actual process/IOPM assertion tests; no kext, MMIO or hardware worker.

The test executable bypasses only AC/lid preflight for desktop CI runners.
The published supervisor is compiled without that macro.
"""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='g0-guard-test-') as directory:
    p=Path(directory)
    common=['clang','-std=c11','-D_DARWIN_C_SOURCE','-Wall','-Wextra','-Werror','-I'+str(ROOT),
            '-framework','CoreFoundation','-framework','IOKit']
    supervisor=p/'g614jvr-probe-supervisor'
    subprocess.run(common+['-DG0_GUARD_TEST_PREFLIGHT',str(ROOT/'Tools/GPI0Observer/supervisor.c'),'-o',str(supervisor)],check=True)
    (p/'test-worker.c').write_text(r'''
#include "Tools/GPI0Observer/GuardSupport.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void) {
 G0GuardToken t={0};
 if(read(G0_GUARD_FD,&t,sizeof(t))!=sizeof(t)||t.magic!=G0_GUARD_MAGIC||
    t.supervisor!=getppid()||!g0_guard_active(t.supervisor)) return 3;
 close(G0_GUARD_FD); puts("WORKER_GUARD_VERIFIED"); fflush(stdout);
 const char *mode=getenv("G0_TEST_MODE");
 if(mode&&!strcmp(mode,"crash")) {raise(SIGKILL);return 4;}
 if(mode&&(!strcmp(mode,"hang")||!strcmp(mode,"signal"))) {
   if(!strcmp(mode,"hang")) signal(SIGTERM,SIG_IGN);
   sleep(100);return 5;
 }
 return 0;
}
''')
    subprocess.run(common+[str(p/'test-worker.c'),'-o',str(p/'g614jvr-gpi0-worker')],check=True)
    for mode in ('normal','crash','signal','hang'):
        start=time.monotonic()
        proc=subprocess.Popen([str(supervisor)],env={**os.environ,'G0_TEST_MODE':mode},stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if mode=='signal':
            assert proc.stdout.readline().strip()=='WORKER_GUARD_VERIFIED'
            proc.send_signal(signal.SIGTERM)
        out,err=proc.communicate(timeout=34)
        duration=time.monotonic()-start
        assert 'GUARD_RELEASED=YES' in err,(mode,out,err)
        assert proc.returncode==(0 if mode=='normal' else 1),(mode,out,err)
        assert duration<30,(mode,duration)
        if mode in ('crash','signal'): assert duration>=4.8,(mode,duration)
        if mode=='hang': assert duration>=24.8,(mode,duration)
        print(f'PASS supervisor {mode}: {duration:.2f}s',flush=True)
    assertions=subprocess.check_output(['pmset','-g','assertions'],text=True)
    assert 'G614JVR G0 supervisor (30s maximum)' not in assertions,'leftover sleep assertion'
print('PASS independent guard: normal, killed worker, SIGTERM grace, hung worker expiry; no GPIO access')
