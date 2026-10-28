"""Static regression guard for the deliberately passive probe entry point."""
from pathlib import Path
import re

src = (Path(__file__).resolve().parents[2] / 'KernelProbe/G614JVRProbe.cpp').read_text()
start = src.split('bool G614JVRProbe::start(IOService* provider) {', 1)[1]
body = start.split('\nIOReturn G614JVRProbe::probeOnce', 1)[0]
assert 'PASSIVE_POWER_CHILD_HOLD_UNSUPPORTED' in body
assert 'registerService()' in body
for symbol in ('probeOnce', 'mapWhenUsable', 'watchSPI', 'watchGPIO', 'run()', 'acquire('):
    assert not re.search(r'\b' + re.escape(symbol), body), symbol
assert not re.search(r'\bnewUserClient\s*\(', src), 'unreviewed trigger introduced'
print('PASS passive start has no probe invocation or user-client trigger; not a hardware safety proof')
