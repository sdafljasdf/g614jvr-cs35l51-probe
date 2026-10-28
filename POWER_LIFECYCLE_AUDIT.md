# Probe power lifecycle audit — publication gate retained

This is an implementation and simulation audit, not a live hardware test.

- SPI3 is validated and opened normally without seize. BAR mapping was separated from open and is attempted only inside a usable-power access section.
- The candidate registers the probe as an interested driver, requests `makeUsable` for SPI3 only, observes capabilities/ordinal, and uses `activityTickle` without hard-coding an ordinal. A zero or unconfirmed capability fails closed. It neither registers itself as the provider's power driver nor writes PCI PMCSR.
- GPIO is separately observed as a power domain and is not forced usable. If its usable capability cannot be confirmed, no GPIO mapping or write occurs.
- An IOLock serializes each individual identity transfer and its CS release with interest callbacks. A pending indication is set atomically before the callback waits, so a subsequent transfer cannot start. Cleanup abandons controller context without MMIO when either domain is unavailable. Interests are deregistered before the two providers close.
- Eight deterministic simulations of the actual PowerGate/SPI3Lease passed. These simulations do not establish real IOKit scheduling or physical GPIO state.

## Explicit blocker: bounded power acknowledgement

`powerStateWillChangeTo` uses a synchronous `IOLockLock` to wait for the current gated section. SPI transfer loops are bounded in code, but a gate may also be held across IOKit memory mapping and other kernel calls, and there is no independent deadline on lock acquisition. A stalled gated section can therefore delay the PM callback beyond the requested short finite window. The added negative simulation deliberately demonstrates a callback remaining blocked while a section is held; it is evidence of this limitation, not a passing boundedness test.

A correct fix must guarantee safe cancellation and CS cleanup without touching a powered-down domain, and must avoid a race between delayed acknowledgement and callback return. Do not replace the lock with an unverified timeout that could acknowledge power-down while MMIO remains active. Until this is resolved:

PM_LIFECYCLE = INCOMPLETE
BOUNDED_POWER_ACK = NOT_PROVEN
WRITE_AUDIT = INCOMPLETE
PUBLICATION_GATE = RETAINED
READY_FOR_REAL_SPI_DEPLOYMENT = NO

No kext was loaded, no EFI was modified, and no real SPI or GPIO transaction occurred in this phase.
