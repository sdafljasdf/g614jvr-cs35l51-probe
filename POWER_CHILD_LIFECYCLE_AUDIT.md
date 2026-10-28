# Power-child lifecycle audit — unsupported pending contracts

**POWER_CHILD_HOLD_UNSUPPORTED. Publication gate retained.** This is a
read-only source and IORegistry assessment. The probe has been changed to
publish a passive service; its legacy `probeOnce` remains in the source but is
not invoked or exposed by a user client. No real device access was performed.

## Confirmed evidence

* The SDK `IOPM.h` describes `kIOPMDeviceUsable` as usable **only as a
  capability**, whereas `kIOPMPowerOn` is valid as an input requirement and
  output power character. They must not be conflated.
* The open-source Apple IOPCIFamily PCI power table uses `kIOPMPowerOn` in
  the device-on state's output power character; `IOPCIDevice::attach`
  registers that table. This is useful platform evidence, not proof of the
  exact running binary or of successful attachment to this specific node.
* `IOService::joinPMtree` has a void return and its default implementation
  delegates attachment to the platform expert; the provider may override it.
  A call returning is not itself proof of the final parent-child relationship.
* XNU `requestPowerDomainState` masks unsupported flags before mapping a
  request to a state. A child cannot assume a requested flag was honored just
  because it registered a state. A verified parent-output contract is needed.
* Read-only IOPower registry shows SPI3 as `IOPCIDevice`, PCI 8086:7a79,
  with `CurrentPowerState=2` and `CapabilityFlags=258` at observation time.
  That snapshot is not a persistent power hold, an output-power table, or
  proof of MMIO accessibility. Ordinal 2 is not treated as a universal ON.
* CS1 uses a separate GPI0 provider. A hold on SPI3 by itself does not prove
  the GPIO domain remains accessible. Both power relationships and any shared
  parent behavior must be established before GPIO mapping or selection.
* The rejected deadline-only counterexamples remain in
  `BOUNDED_QUIESCE_COUNTEREXAMPLES.md`.

## Unresolved, not implemented

The exact runtime SPI3 power-parent output contract, verified join placement,
and independent GPI0 power hold/access contract remain unknown. A system idle
sleep assertion would not prevent forced sleep, shutdown, provider termination,
or removal. Their arbitration and cleanup need an independently evidenced
contract. Nor does a sleep assertion survive loss of its owning process.
No power child, user client, trigger tool, hold, or sleep assertion is
implemented. The stale legacy method is not a fallback and must not become
reachable without a complete new lifecycle audit.

POWER_CHILD_JOIN = NOT IMPLEMENTED
PARENT_POWER_REQUIREMENT = UNKNOWN (kIOPMPowerOn is a candidate, not certified)
GPIO_POWER_HOLD = UNKNOWN
POWER_HOLD_BEFORE_MMIO = NOT IMPLEMENTED
USERLAND_SLEEP_ASSERTION = NOT IMPLEMENTED
OLD_DEADLINE_MODEL = REJECTED
READY_FOR_REAL_SPI_DEPLOYMENT = NO

## Pinned public sources

* Apple IOPCIFamily commit `4822b27a36e2de70e231ecf2bf3021384fab6ec2`,
  `IOPCIBridge.cpp` SHA256 `7bd0894dcb1ef5628ffc3404fccd9949208396c00380307939346c9fa8e7c5af`;
  `IOPCIDevice.cpp` SHA256 `1139124b264715a675154f0cee6d64fa81c72860f73286052205ac969fe2ffa5`.
* Apple XNU commit `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea`,
  `iokit/Kernel/IOServicePM.cpp` SHA256 `c1122cb7ac09542614821ddeff8ce211bc982599aa88202ea0e365724a5a6589`.

These are public reference revisions, not a claim that they exactly match the
installed Tahoe kernel or PCI-family build.
