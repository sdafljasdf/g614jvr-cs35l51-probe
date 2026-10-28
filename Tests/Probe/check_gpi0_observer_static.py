"""Narrow source checks, not a substitute for machine-code or lifecycle audit."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
cpp = (root / 'KernelGPI0Observer/G614JVRGPI0Observer.cpp').read_text()
header = (root / 'KernelGPI0Observer/GPI0ReadOnly.hpp').read_text()
assert 'kIOMapReadOnly | kIOMapInhibitCache' in cpp
assert 'getMapOptions()' in cpp and 'options & kIOMapReadOnly' in cpp
assert 'const volatile uint32_t*' in cpp
assert 'IOACPIPlane:/_SB/GPI0' in cpp and 'getPhysicalAddress() != 0xe0690000ULL' in cpp
for forbidden in ('OSWrite', 'writel(', 'gpioWrite', 'setDirection', 'configureGPIO', 'newUserClient'):
    assert forbidden not in cpp + header, forbidden
start = cpp.split('bool G614JVRGPI0Observer::start(', 1)[1].split('IOReturn G614JVRGPI0Observer::observe(', 1)[0]
assert not re.search(r'\bobserve\s*\(', start)
assert 'PASSIVE_NO_TRIGGER' in start
print('PASS observer source is passive, read-only mapping requested, no listed write calls')
