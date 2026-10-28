// IOKit interested-client coordination. No PM policy, PCI PMCSR, or reset writes.
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
#include <kern/kern_types.h>

namespace g614jvr_probe {
class PowerGate final {
    struct Domain {
        IOService* provider = nullptr;
        IOPMPowerFlags capabilities = 0;
        unsigned long ordinal = 0;
        volatile SInt32 pending = 0;
        volatile SInt32 generation = 0;
        bool registered = false, usable = false;
    };
public:
    explicit PowerGate(IOService* owner) : owner_(owner), lock_(IOLockAlloc()) {}
    PowerGate(const PowerGate&) = delete;
    PowerGate& operator=(const PowerGate&) = delete;
    ~PowerGate() { close(); if (lock_) IOLockFree(lock_); }
    bool valid() const { return owner_ && lock_; }

    IOReturn watchSPI(IOService* provider) { return watch(spi_, provider, true); }
    // GPIO is observed, but never explicitly raised or otherwise forced.
    IOReturn watchGPIO(IOService* provider) { return watch(gpio_, provider, false); }

    // Callbacks must be forwarded by the service. The pending flag is set
    // before taking the lock: no new hardware section can pass a queued will.
    IOReturn will(IOService* provider) {
        Domain* domain = find(provider);
        if (!domain || !lock_) { OSCompareAndSwap(0, 1, &unknown_); return IOPMAckImplied; }
        OSIncrementAtomic(&domain->generation);
        if (!OSCompareAndSwap(0, 1, &domain->pending)) OSCompareAndSwap(0, 1, &unknown_);
        IOLockLock(lock_); domain->usable = false; IOLockWakeup(lock_, this, false); IOLockUnlock(lock_);
        return IOPMAckImplied;
    }
    IOReturn did(IOService* provider, IOPMPowerFlags capabilities, unsigned long ordinal) {
        Domain* domain = find(provider);
        if (!domain || !lock_) { OSCompareAndSwap(0, 1, &unknown_); return IOPMAckImplied; }
        OSIncrementAtomic(&domain->generation);
        IOLockLock(lock_);
        domain->capabilities = capabilities; domain->ordinal = ordinal;
        domain->usable = (capabilities & kIOPMDeviceUsable) != 0;
        OSCompareAndSwap(1, 0, &domain->pending);
        IOLockWakeup(lock_, this, false); IOLockUnlock(lock_);
        return IOPMAckImplied;
    }

    // Successful begin holds the single lock until end(). Each hardware
    // operation must be bounded; callbacks cannot acknowledge power-down until
    // the current operation and its CS release leave this section.
    bool begin(bool requireGPIO, bool allowWait = false) {
        if (!lock_) return false;
        // A second iteration is a bounded PM wait/recheck, never an SPI retry.
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            IOLockLock(lock_);
            if (shutdown_ || active_ || OSAddAtomic(0, &unknown_)) { IOLockUnlock(lock_); return false; }
            if (!ready(spi_) || (requireGPIO && !ready(gpio_))) {
                if (!allowWait || attempt || !waitLocked(requireGPIO)) { IOLockUnlock(lock_); return false; }
            }
            const auto ordinal = spi_.ordinal;
            const auto generation = OSAddAtomic(0, &spi_.generation);
            IOLockUnlock(lock_);
            // Never invoke a foreign PM API while holding the callback lock.
            const bool tickled = spi_.provider->activityTickle(kIOPMSuperclassPolicy1, ordinal);
            IOLockLock(lock_);
            const bool unchanged = OSAddAtomic(0, &spi_.generation) == generation && spi_.ordinal == ordinal;
            if (!tickled && unchanged) spi_.usable = false;
            if (!tickled || !unchanged || shutdown_ || active_ || OSAddAtomic(0, &unknown_) ||
                !ready(spi_) || (requireGPIO && !ready(gpio_))) {
                if (!allowWait || attempt || !waitLocked(requireGPIO)) { IOLockUnlock(lock_); return false; }
                IOLockUnlock(lock_);
                continue;
            }
            // A callback arriving after active_ is set waits for this bounded
            // section; final recheck rejects one that arrived before it.
            active_ = true;
            if (OSAddAtomic(0, &unknown_) || !ready(spi_) || (requireGPIO && !ready(gpio_))) {
                active_ = false; IOLockUnlock(lock_); return false;
            }
            return true;
        }
        return false;
    }
    void end() { active_ = false; IOLockUnlock(lock_); }
    bool active() const { return active_; }

    void close() {
        if (!lock_) return;
        IOLockLock(lock_); shutdown_ = true; IOLockWakeup(lock_, this, false); IOLockUnlock(lock_);
        // Deregistration synchronizes all in-flight interest callbacks. Do not
        // hold the access lock while waiting for callback completion.
        if (gpio_.registered) { gpio_.provider->deRegisterInterestedDriver(owner_); gpio_.registered = false; }
        if (spi_.registered) { spi_.provider->deRegisterInterestedDriver(owner_); spi_.registered = false; }
    }
private:
    static bool ready(const Domain& d) { return d.provider && d.registered && d.usable && OSAddAtomic(0, const_cast<volatile SInt32*>(&d.pending)) == 0; }
    Domain* find(IOService* p) { return p && p == spi_.provider ? &spi_ : p && p == gpio_.provider ? &gpio_ : nullptr; }
    IOReturn watch(Domain& domain, IOService* provider, bool requestUsable) {
        if (!valid() || !provider || domain.provider) return kIOReturnBadArgument;
        domain.provider = provider;
        const auto initial = provider->registerInterestedDriver(owner_);
        domain.registered = true;
        owner_->setProperty(requestUsable ? "SPI3_POWER_CAPABILITIES_INITIAL" : "GPIO_POWER_CAPABILITIES_INITIAL", uint64_t(initial), 64);
        owner_->setProperty(requestUsable ? "SPI3_POWER_STATE_INITIAL" : "GPIO_POWER_STATE_INITIAL", uint64_t(provider->getPowerState()), 32);
        owner_->setProperty(requestUsable ? "SPI3_POWER_INTEREST_REGISTERED" : "GPIO_POWER_INTEREST_REGISTERED", kOSBooleanTrue);
        // Zero means no defined power state; registration still gets deregistered.
        if (!initial) return kIOReturnNotReady;
        if (requestUsable) {
            const auto requested = provider->makeUsable();
            owner_->setProperty("SPI3_MAKE_USABLE_RETURN", uint64_t(uint32_t(requested)), 32);
            if (requested != kIOReturnSuccess) return kIOReturnNotReady;
        }
        const auto generation = OSAddAtomic(0, &domain.generation);
        const auto current = provider->currentCapability();
        const auto ordinal = provider->getPowerState();
        IOLockLock(lock_);
        if (OSAddAtomic(0, &domain.pending) == 0 && OSAddAtomic(0, &domain.generation) == generation &&
            (current & kIOPMDeviceUsable)) {
            domain.capabilities = current; domain.ordinal = ordinal; domain.usable = true;
        }
        const bool readyNow = ready(domain);
        if (!readyNow && requestUsable) (void)waitDomainLocked(domain);
        const bool readyAfterWait = ready(domain);
        const auto observedOrdinal = domain.ordinal;
        const auto observedCapabilities = domain.capabilities;
        IOLockUnlock(lock_);
        owner_->setProperty(requestUsable ? "SPI3_POWER_STATE_AFTER_MAKEUSABLE" : "GPIO_POWER_STATE_OBSERVED", uint64_t(observedOrdinal), 32);
        owner_->setProperty(requestUsable ? "SPI3_POWER_CAPABILITIES" : "GPIO_POWER_CAPABILITIES", uint64_t(observedCapabilities), 64);
        return readyAfterWait ? kIOReturnSuccess : kIOReturnNotReady;
    }
    bool waitDomainLocked(Domain& domain) {
        uint64_t deadline = 0; clock_interval_to_deadline(2000, 1000000, &deadline);
        while (!shutdown_ && !ready(domain)) {
            if (IOLockSleepDeadline(lock_, this, deadline, THREAD_ABORTSAFE) != THREAD_AWAKENED) break;
        }
        return !shutdown_ && ready(domain);
    }
    bool waitLocked(bool requireGPIO) {
        uint64_t deadline = 0; clock_interval_to_deadline(2000, 1000000, &deadline);
        while (!shutdown_ && (!ready(spi_) || (requireGPIO && !ready(gpio_)))) {
            if (IOLockSleepDeadline(lock_, this, deadline, THREAD_ABORTSAFE) != THREAD_AWAKENED) break;
        }
        return !shutdown_ && ready(spi_) && (!requireGPIO || ready(gpio_));
    }
    IOService* owner_ = nullptr; IOLock* lock_ = nullptr;
    Domain spi_{}, gpio_{}; volatile SInt32 unknown_ = 0; bool active_ = false, shutdown_ = false;
};
}
