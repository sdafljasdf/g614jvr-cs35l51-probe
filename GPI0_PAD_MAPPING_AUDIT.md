# GPI0 pad mapping and read-only observer audit

**Status: static mapping established; live read not performed.** An isolated
passive observer and its supervised, fixed one-shot user client are now built
and tested. They are candidates for deployment, not evidence of live pad
values. The independent SPI probe remains unpublished and undeployed.

## Platform and resource evidence

* IORegistry presents `\_SB.GPI0` as `IOACPIPlatformDevice`, HID `INTC1085`,
  `_STA=15`, with ordered 64 KiB memory resources at `0xe06e0000`,
  `0xe06d0000`, `0xe06b0000`, `0xe06a0000`, and `0xe0690000`. The
  interrupt specifier begins with 14; the historical live DSDT's dynamic
  `_CRS` names `SGIR` as its interrupt. A runtime AML `_CRS` evaluation was
  not performed in this phase. `_UID` is not exposed in the observed registry
  and no `_UID` appears in this DSDT GPI0 definition; runtime UID is UNKNOWN.
* The saved DSDT `GPI0._CRS` constructs five `Memory32Fixed` descriptors in
  that order using SBRG + `0x6e0000`, `0x6d0000`, `0x6b0000`, `0x6a0000`,
  `0x690000`. This dynamic AML and the current registry agree in ordering,
  but registry ranges do not prove that it is safe to map and read them.
* The saved SPK1 resource data identify GPI0:0x01D4 as resource zero, and the
  prior exact-model Linux quirk associates that resource with the second CS.
  Neither static ACPI PullUp nor a TX latch by itself proves actual pin voltage.

## Linux mapping calculation

Pinned Linux `65ba8f34ec886ec72b2c07ddc8cf3d815c32869d`,
`drivers/pinctrl/intel/pinctrl-alderlake.c` SHA256
`78dfea6bcc992aeb2bb78a224f578ae96ee9b9a8965ed41b8e5f5128ff2e6f8f`,
maps `INTC1085` to `adls_soc_data`. The table's fifth ACPI memory resource
(`barno=4`) is community 5; it contains `INTEL_GPP(0, 270, 294, 448)`,
GPP_D. GPIO 0x01D4 is decimal 468; 468-448 = 20, so it maps to pin 290,
GPP_D20, also labelled UART3_RXD as a *possible native function* in that
table. The label does not establish this machine's current pad mode.

The community physical resource starts at `0xe0690000`. Linux reads PADBAR
at offset `0x0c`; its value and REVID have not been sampled on this Mac.
PADCFG0 offset is `PADBAR + 20 * stride`, where stride is 16 bytes for
revision >= 0x92 and 8 otherwise; PADCFG1 follows at +4, PADCFG2 at +8 only
when supported. Thus no actual PADCFG address is asserted without a sample.

PAD_OWN is at offset `0xa8`, bits 19:16, for the older four-bit-per-pad
layout, but on revision >= 0x110 Linux uses offset `0xf0`, bits 2:0. The
existing generic GPIO output implementation assumes the older layout and
must not be reused as proof for an unknown revision. HOSTSW_OWN offset 0x150
bit 20 and configuration / TX lock offsets 0x110 / 0x114 bit 20 are derived
from the table. These are candidate read locations, not live measurements.
The pinned `pinctrl-intel.c` SHA256 is
`de61a650d4db1e188758b442b7efe87dfdfc4aa3032214cd1705722eb1097eef`.

Linux's driver has system sleep callbacks and no runtime-PM callbacks in this
source. This supports the proposed investigation; it does not document the
macOS provider's power or access contract, map resources on macOS, or prove
the target registers have no side effects under every platform state.

## Observer readiness and remaining runtime gates

The dedicated observer described in `GPI0_OBSERVER_AUDIT.md` now implements
a root-only fixed synchronous user client, local read-only uncached mapping,
and a separate supervisor with a system-enforced finite idle-sleep assertion.
It does not reuse the old generic GPIO writer. These implementation and
publication gates permit an observer artifact before the first physical
sample; they do not assert that this hardware has already been read.

The live worker distinguishes TX latch from RX state; when input is disabled,
RX is unknown. `CS1_DESELECTED_NOW=YES` is only a sampled GPIO-mode, TX-enabled,
high-latch and ownership predicate. It is not a measured pin voltage or a
lease guaranteeing that no other actor can change the pad later. The offline
decoder remains synthetic-only and provides no permission for SPI access.
SPI3's independent ownership and power-lifetime gates still apply to A0.
No CS0 or CS1 transfer is performed by the observer.

Deployment has an additional host-specific gate: the current shared Kingston
EFI failed normal read-only mounting, and an offline `fsck_msdos -n` of its
unchanged raw backup reports five overallocated Spotlight index-file chains.
No EFI repair or observer injection was performed. The observer artifact is
ready for its controlled validation; this discovered filesystem issue must
be resolved before modifying that shared EFI. This is separate from the
observer's publication gate and does not require a rescue USB specifically.

Sources: https://github.com/torvalds/linux/blob/65ba8f34ec886ec72b2c07ddc8cf3d815c32869d/drivers/pinctrl/intel/pinctrl-alderlake.c
and https://github.com/torvalds/linux/blob/65ba8f34ec886ec72b2c07ddc8cf3d815c32869d/drivers/pinctrl/intel/pinctrl-intel.c
