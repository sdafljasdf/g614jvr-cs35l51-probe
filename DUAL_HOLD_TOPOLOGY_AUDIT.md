# SPI3 / GPI0 dual-hold topology audit

Conclusion: **GPI0_POWER_HOLD_UNSUPPORTED** for this proposal on the
observed runtime topology. **DUAL_POWER_HOLD_PROVEN = NO.** This means no
safe power-manageable provider and requirement were established, not that the
physical GPIO bank has been proven permanently off or unpowerable.

## Read-only observations

* IOService / IODeviceTree / IOACPIPlane contain `GPI0`, class
  `IOACPIPlatformDevice`, ACPI name `INTC1085`, `_STA=15`, with five
  `IODeviceMemory` resources. Prior verified platform resource evidence maps
  CS1 to `\_SB.GPI0`, pad `0x01D4`; no pad was read or driven here.
* IOService shows `GPI0` under the ACPI platform expert with no child service
  attached to that node in this snapshot. `org.coolstar.VoodooGPIO` is loaded,
  but that fact is **not evidence of attachment to GPI0**. A search of the
  IOService tree for GPI0 / INTC1085 / GPIO found only the GPI0 ACPI node.
* `ioreg -p IOPower -n GPI0 -r` produced no node. No GPI0 power-plane parent,
  state table, output power flags, or power-managed GPIO driver was observed.
  Absence from this snapshot is not proof that no vendor-specific mechanism
  could exist.
* SPI3 is `IOPCIDevice` 8086:7A79 at SPI3@13; in IOPower it is a child of
  `AppleACPIPCI` through an IOKit power connection. Its observed registry
  snapshot has `CurrentPowerState=2`, `CapabilityFlags=258` and
  `MaxPowerState=3`. These are capability and state snapshots, not a child
  hold or proof of the current outputPowerCharacter.
* Apple's public IOPCIFamily source supports `kIOPMPowerOn` as a candidate
  input requirement for a PCI child. Runtime joining, output flags and
  continuous hold have not been tested. `joinPMtree()` itself returns void;
  a successful invocation would not by itself prove connection placement.

## Consequences

The user-specified stop condition applies before implementing GPI0PowerHold:
there is no confirmed power-plane GPI0 provider or supported GPIO-access
requirement. Two helper services, a coordinator and access to hardware are
not implemented. A coordinator-only simulation could establish ordering
under assumptions, but would not establish the actual GPIO power contract;
it must not be reported as the requested dual-hold proof. Existing passive
startup and publication gate remain in place. No deadline/cancellation
fallback was enabled.

```text
SPI3_POWER_PARENT = AppleACPIPCI (IOPower snapshot)
SPI3_POWER_REQUIREMENT = kIOPMPowerOn (candidate, not runtime verified)
SPI3_HOLD_JOINED = NO
SPI3_HOLD_ACTIVE_MODEL = NOT IMPLEMENTED
GPI0_ACPI_NODE = \\_SB.GPI0 / INTC1085
GPI0_IOKIT_PROVIDER = IOACPIPlatformDevice GPI0 (resource node only)
GPI0_ATTACHED_DRIVER = NONE OBSERVED (VoodooGPIO kext is loaded)
GPI0_POWER_PARENT = NOT OBSERVED
GPI0_IN_POWER_PLANE = NO (current snapshot)
GPI0_POWER_STATES = NOT OBSERVED
GPI0_POWER_REQUIREMENT = UNKNOWN
GPI0_HOLD_JOINED = NO
GPI0_HOLD_ACTIVE_MODEL = NOT IMPLEMENTED
DUAL_HOLD_COORDINATOR = NOT IMPLEMENTED
HARDWARE_ACCESS_REQUIRES_BOTH = NOT IMPLEMENTED; ENTRY REMAINS PASSIVE
MULTI_PARENT_DEPENDENCY_USED = NO
GPI0_POWER_HOLD_UNSUPPORTED
DUAL_POWER_HOLD_PROVEN = NO
```

Relevant source revisions and hashes are recorded in
`POWER_CHILD_LIFECYCLE_AUDIT.md`. `ioreg` and `kmutil showloaded` were read
only; no probe kext was loaded, no GPIO or SPI resource was mapped or
accessed, and no EFI or driver configuration was changed.
