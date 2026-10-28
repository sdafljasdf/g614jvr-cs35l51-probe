# Phase G0 read-only observer candidate

**Passive, unpublished and not ready for real hardware.** No real register
was read and no kext was loaded or installed. This commit is a partial
implementation, not a live G0 result.

## Source evidence

Apple SDK `IOMemoryDescriptor.h` documents `kIOMapReadOnly`, inhibited cache,
mapping options inspection, and that the device memory descriptor is owned
by the provider. XNU `IOMemoryDescriptor.cpp` at commit
`f6217f891ac0bb64f3d375211650a4c1ff8ca1ea` uses read-only VM protection
when the flag is present and avoids reusing a writable mapping for a read-only
request. This addresses writes to that virtual mapping; it does not prove MMIO
reads are harmless or the controller is powered.

## Candidate

`G614JVRGPI0Observer` is a separate target and contains no GPIO or SPI write
interface. Its `start()` publishes `PASSIVE_NO_TRIGGER`; no user client or
other trigger exists. The unreachable private candidate method verifies the
IOService path and `INTC1085` property, requests memory index 4 of exactly
64 KiB with `kIOMapReadOnly | kIOMapInhibitCache`, checks returned options and
size, then uses only a const volatile 32-bit read interface. The finite
sample sequence reads REVID, CAPLIST, PADBAR, then derived PADCFG0/1 and
specific ownership/lock words; invalid values and bounds cause failure.
The resource-4 / GPP_D20 and revision-dependent ownership calculations are
documented in `GPI0_PAD_MAPPING_AUDIT.md`.

Static checks and synthetic reader tests verify the sampled offsets, rejected
PADBAR bounds, read-only options requested, absent known write symbols and
passive entry. These checks are not a complete machine-code proof or a live
test. On returned sample values the code records raw data only and refuses to
claim CS1 is physically deselected; TX state alone does not guarantee pin
voltage or its persistence while another agent may change the pad.

## Missing gates

* A user-space client draft verifies AC and lid state, requires an explicit
  protocol-ready property, obtains a temporary idle-sleep assertion and
  releases it after a synchronous call. The current kernel exports neither
  that property nor a user client, so this is not an operational trigger.
  Cancellation and process-death handling do not exist; the process assertion
  must outlive all accesses.
* The private candidate method now compares both IOService and ACPI-plane
  registry paths, and requires resource 4 to have the recorded physical base
  and 64 KiB length. These are source checks only; they have not been executed
  in the kernel, and no live AML `_CRS` descriptor evaluation was done.
* GPI0 has no confirmed macOS power-plane or access contract; even a read-only
  mapping and Linux's lack of runtime suspend callbacks cannot rule out a
  platform bus fault, forced sleep, concurrent access, or side effects on the
  actual device. The requested fixed register read semantics are supported by
  Linux usage, not independently verified on this hardware.
* No authenticated user-client interface or cleanup under process death is
  implemented. No publication gate has been removed. The older SPI probe's
  separate CI gate is unchanged.

READ_ONLY_MAPPING = REQUESTED_AND_CHECKED_IN_UNREACHABLE_SOURCE
MAP_READ_ONLY_RUNTIME = UNKNOWN
GPI0_WRITES = 0 (static candidate path; no live access)
REAL_GPIO_READ_OCCURRED = NO
REAL_SPI_READ_OCCURRED = NO
CS1_DESELECTED_NOW = UNKNOWN
PUBLICATION = BLOCKED
