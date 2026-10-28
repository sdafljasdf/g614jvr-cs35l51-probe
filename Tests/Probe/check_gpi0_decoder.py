"""Offline synthetic samples only; no hardware access."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Research/G614JVR'))
from gpi0_pad_decode import decode, layout

old = layout(0x00940000, 0x700)
assert (old.padcfg0_offset, old.padown_offset, old.padown_shift, old.padown_mask) == (0x840, 0xa8, 16, 15)
new = layout(0x01100000, 0x700)
assert (new.padcfg0_offset, new.padown_offset, new.padown_shift, new.padown_mask) == (0x840, 0xf0, 0, 7)
legacy = layout(0x00910000, 0x700)
assert legacy.padcfg0_offset == 0x7a0
sample = decode(old, 0, 1 << 20, 1 | (1 << 9), 0, 0, 0)
assert sample['pad_owned_by_host'] and sample['hostsw_gpio_mode']
assert sample['tx_latch'] == 1 and sample['rx_state'] is None
assert sample['cs1_deselected_now'] is None
assert not decode(new, 2, 0, 0, 0, 0, 0)['pad_owned_by_host']
for args in ((0xffffffff, 0x700), (0x00940000, 0xfff0)):
    try:
        layout(*args)
    except ValueError:
        pass
    else:
        raise AssertionError('invalid layout accepted')
try:
    decode(old, 0xffffffff, 0, 0, 0, 0, 0)
except ValueError:
    pass
else:
    raise AssertionError('unavailable register accepted')
print('PASS synthetic GPI0 decode; no live samples and no CS1 safety assertion')
