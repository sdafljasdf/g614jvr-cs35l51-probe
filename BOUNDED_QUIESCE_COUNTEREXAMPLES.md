# Phase A bounded-quiesce review

Status: **FAIL / publication gate retained**. This analysis is of the candidate
at 6059447; no hardware access was made.

1. `PowerGate::will()` currently takes `IOLockLock` while the worker can hold
   it during controller or GPIO mapping and transfer. It cannot meet the
   requested non-blocking callback requirement.
2. Neither `mapDeviceMemoryWithRegister()` nor `mapDeviceMemoryWithIndex()`
   has a demonstrated bounded or cancellable completion contract here. A
   worker checking cancellation after the call returns does not prove it will
   return before the PM acknowledgement budget. Move mapping outside any
   active-transaction claim and recheck all power generations afterwards, but
   do not assume this alone makes access safe.
3. A check of an atomic flag and clock before an MMIO instruction is not an
   atomic check-and-access operation. A worker can be preempted after the
   check, then resume after a power transition. A bounded polling loop and a
   compile-time sum of constants do not bound scheduler suspension, an MMIO
   instruction or an external IOKit call. The deterministic model in
   `Tests/Probe/check_deadline_counterexamples.py` reproduces this interleaving
   and a blocked mapping call; it tests the model, not real hardware.
4. Returning an asynchronous acknowledgement budget does not itself stop a
   worker. Apple XNU's `ackTimerTick` documents that a tardy interested driver
   can be treated as acknowledged. A correct exactly-once acknowledgement
   protocol also needs to account for callback-return ordering and separate
   SPI3 and GPIO power transitions. No such protocol is claimed here.

The proposed atomic-cancellation/deadline redesign is not installed in the
probe: without a demonstrated supported primitive that prevents the OS from
removing power while the final MMIO operation can execute, it would only make
the defect less visible. A viable redesign requires a documented provider or
power-domain access contract that ties actual hardware access to PM
arbitration, and a bounded mapping/cleanup strategy. If those contracts cannot
be established, remain fail-closed. Do not publish or deploy this candidate.

```text
PM_CALLBACK_LOCK_WAIT = NONZERO
ASYNC_ACK_EXACTLY_ONCE = NOT IMPLEMENTED
HARDWARE_AFTER_PM_DEADLINE = NOT EXCLUDED
LIFECYCLE_AUDIT = FAIL
READY_FOR_REAL_SPI_DEPLOYMENT = NO
```

Reference: https://github.com/apple-oss-distributions/xnu/blob/main/iokit/Kernel/IOServicePM.cpp
