"""Pure decoder for supplied GPP_D20 samples. No device, file, or MMIO access.

Register offsets and meanings follow the pinned Linux source in the audit.
This module does not acquire samples and cannot establish live CS1 safety.
"""
from dataclasses import dataclass

MASK32 = 0xffffffff


@dataclass(frozen=True)
class Layout:
    padcfg0_offset: int
    padown_offset: int
    padown_shift: int
    padown_mask: int
    hostsw_offset: int = 0x150
    cfg_lock_offset: int = 0x110
    tx_lock_offset: int = 0x114


def layout(revid: int, padbar: int, resource_size: int = 0x10000) -> Layout:
    if not 0 <= revid < MASK32 or not 0 < padbar < resource_size or padbar % 4:
        raise ValueError('invalid revision or PADBAR')
    revision = revid >> 16
    stride = 16 if revision >= 0x92 else 8
    cfg = padbar + 20 * stride
    if cfg + stride > resource_size:
        raise ValueError('pad configuration outside resource')
    if revision >= 0x110:
        return Layout(cfg, 0xa0 + 20 * 4, 0, 7)
    return Layout(cfg, 0xa0 + (20 // 8) * 4, (20 % 8) * 4, 15)


def decode(ly: Layout, padown: int, hostsw: int, cfg0: int, cfg1: int,
           cfg_lock: int, tx_lock: int) -> dict:
    words = (padown, hostsw, cfg0, cfg1, cfg_lock, tx_lock)
    if any(not 0 <= word < MASK32 for word in words):
        raise ValueError('invalid or unavailable register')
    return {
        'pad_owned_by_host': ((padown >> ly.padown_shift) & ly.padown_mask) == 0,
        'hostsw_gpio_mode': bool(hostsw & (1 << 20)),
        'pad_mode': (cfg0 >> 10) & 15,
        'tx_disabled': bool(cfg0 & (1 << 8)),
        'rx_disabled': bool(cfg0 & (1 << 9)),
        'tx_latch': cfg0 & 1,
        'rx_state': None if cfg0 & (1 << 9) else (cfg0 >> 1) & 1,
        'termination_code': (cfg1 >> 10) & 7,
        'termination_up': bool(cfg1 & (1 << 13)),
        'configuration_locked': bool(cfg_lock & (1 << 20)),
        'tx_locked': bool(tx_lock & (1 << 20)),
        # A software decoder cannot prove physical voltage, exclusive ownership,
        # stability until an SPI transfer, or that a supplied sample is live.
        'cs1_deselected_now': None,
    }
