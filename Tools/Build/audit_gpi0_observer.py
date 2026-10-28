"""Source/symbol publication gate for the isolated G0 target.
Not a claim about physical voltage or forced-sleep immunity.
"""
from pathlib import Path
import re
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
src=(root/'KernelGPI0Observer/G614JVRGPI0Observer.cpp').read_text()
hdr=(root/'KernelGPI0Observer/GPI0ReadOnly.hpp').read_text()
supervisor=(root/'Tools/GPI0Observer/supervisor.c').read_text()
worker=(root/'Tools/GPI0Observer/worker.c').read_text()
workflow=(root/'.github/workflows/build-gpi0-observer.yml').read_text()
assert 'G0_UNREVIEWED_LIVE_TRIGGER' not in src
assert 'READY_G0_SUPERVISED_V2' in src and 'READY_G0_SUPERVISED_V2' in worker
assert 'MapScope mapping(map)' in src and 'map->getMapOptions()' in src
assert 'kIOMapReadOnly | kIOMapInhibitCache' in src
assert 'IOACPIPlane:/_SB/GPI0' in src and '0xe0690000ULL' in src
assert 'clientMemoryForType' not in src
assert 'OSCompareAndSwap(0, 6, &state_)' in src and 'finishClosing' in src
assert 'G0_GUARD_TEST_PREFLIGHT' not in workflow
assert 'kIOPMAssertionTimeoutActionRelease' in supervisor and 'posix_spawn(' in supervisor
assert 'g0_guard_active(token.supervisor)' in worker
for forbidden in ('OSWrite','writel(','gpioWrite','setGPIO','setDirection','setPull','setMux','CirrusAudioFixup/'):
    assert forbidden not in src+hdr,forbidden
assert len(re.findall(r'\*reinterpret_cast<const volatile uint32_t\*>',src))==1
symbols=subprocess.check_output(['nm','-j',sys.argv[1]],text=True)
for forbidden in ('OSWrite','writel','gpioWrite','setDirection','configureGPIO','CirrusAudioFixup','CS35L41Driver','LPSSSPI'):
    assert forbidden not in symbols,forbidden
assert 'G614JVRGPI0UserClient' in symbols and 'externalMethod' in symbols
print('PASS G0 publication gate: passive service, fixed root-only sync method, local RO map, independent expiring supervisor, no GPIO/SPI write dependency')
