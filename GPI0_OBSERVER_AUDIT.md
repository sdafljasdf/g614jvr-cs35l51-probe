# G0 supervised read-only observer

Scope: a controlled, one-shot observation of G614JVR `GPI0/INTC1085`,
resource 4, GPP_D20. This artifact is a candidate for its first real read;
compilation and tests are not evidence of a physical register value.
The independent SPI-probe publication gate stays closed.

## Passive service and fixed interface

Loading starts no mapping or read. A root-only IOUserClient exposes selector
0 with no input and a fixed 80-byte output structure (protocol 2). Async
requests, scalar inputs/outputs, descriptor arguments, wrong sizes and all
other selectors are rejected. No user mapping or arbitrary offset/address is
provided. Each connection admits only one call; a service-wide atomic gate
also rejects observations concurrent across connections.

The provider must be the same object at both exact IOService and ACPI paths,
be an ACPI platform device named INTC1085, expose at least five memory ranges,
and grant a normal non-seizing open. Resource 4 must match the observed
`0xe0690000`, 65536-byte range. Failure stops before any read.

## Mapping and fixed reads

Only `kIOMapReadOnly | kIOMapInhibitCache` is requested. Returned flags,
length and virtual address are checked. No writable fallback exists. XNU's
IOMemoryDescriptor implementation maps the read-only option to VM read
protection and avoids substituting a writable mapping. No test write is made.

A local RAII MapScope releases every successful map on all returns; it is
destroyed before provider close and before the result is copied to the user
buffer. The service stores a provider reference, never a mapping. Its only
MMIO primitive dereferences a `const volatile uint32_t*` for a load, checks
cancellation/bounds, and caps accesses at nine. The fixed sampler requests:
REVID (0), CAPLIST (4), PADBAR (0xc), the computed PADCFG0 and PADCFG1, and
only the target PAD_OWN, HOSTSW_OWN, PADCFGLOCK and PADCFGLOCKTX words.
No capability chain, IRQ status, adjacent pad, retry or register scan occurs.

The pinned Linux layout is documented in `GPI0_PAD_MAPPING_AUDIT.md`.
Revisions outside 0x92..0x110 fail closed; the decoded Linux feature selects
16-byte pad stride and the revision-dependent ownership layout. PADBAR must
be aligned, at least 0x200, and every derived access must fit the resource.
The worker reports partial read count and failure status without treating
partial samples as valid pad state.

## Close, death and finite sleep guard

Client close/death atomically closes admission. If a method is active, client
termination is deferred until that method has returned from the local map
scope; the method retains the client and the client retains the service.
Per-load cancellation skips remaining reads and performs only unmapping and
reference cleanup. There is no background kernel worker or persistent map.

`g614jvr-probe-supervisor` and `g614jvr-gpi0-worker` are separate processes.
The supervisor checks AC/open lid, creates an IOPM idle-system-sleep assertion
with a system-enforced 30-second release timeout, and launches only its fixed
sibling worker. A pipe token carries its PID and a 20-second worker deadline.
The worker verifies parent identity and an active assertion under that PID
using public IOPMCopyAssertionsByProcess, as well as AC/lid and protocol.
It never owns the assertion. A normal successful return releases it
immediately; worker error/crash or SIGINT/SIGTERM keeps it for a 5-second
grace. A hung worker is terminated at 20 seconds, given that grace, and then
killed; the assertion is released by 30 seconds even if the supervisor hangs.
No permanent PM settings, daemon or password storage is created.

This is a finite controlled experiment window, not hard real-time or forced
sleep immunity. SIGKILL of the supervisor, lid closure, manual sleep, forced
shutdown and thermal emergency are outside the guarantee. A pathological
kernel/MMIO stall beyond the guard's lifetime is not made safe by a timer.
Keep AC connected and lid open, and do not request sleep during observation.
Unexpected resource, invalid register data or access failure stops the run.

## Verification and publication

* Host compilation of the actual observer source against IOKit fakes verifies
  passive startup, root/selector/resource/map rejection, map destruction,
  close/death during mapping, and cross-client concurrent rejection. Samples
  in this test are allocated host memory, never physical GPIO.
* Actual process/IOPM tests exercise successful completion, killed worker,
  supervisor SIGTERM and hung-worker expiry. Only CI test preflight bypasses
  AC/lid for desktop runners; published binaries contain no preflight bypass.
* CI builds the isolated x86_64 kernel target and two tools, verifies IOKit
  vtable pairing and forbidden write/SPI/amp symbols, runs the source gate,
  then packages hashes and commit provenance. First real read is the next
  validation step, not a publication prerequisite.

Only the observer artifact may be injected into EFI. Never use `kmutil load`
to install it on this system: macOS may then rebuild AuxKC. Do not alter
NullMoth, the audio host stack, ACPI, or Windows. After the required cold
boot, check existing drivers and graphics before triggering G0 once.

Sources: Apple SDK IOService/IOUserClient/IOMemoryDescriptor/IOPMLib headers;
XNU IOMemoryDescriptor and IOUserClient sources; pinned Linux sources in the
mapping audit. Apple documents assertion limits at
https://developer.apple.com/library/archive/qa/qa1340/_index.html .
