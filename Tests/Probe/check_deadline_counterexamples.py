"""Counterexamples to treating cooperative deadlines as hard PM safety.

This is a pure scheduling model. It never opens a device or executes MMIO.
Passing means the counterexamples were reproduced, not that the probe is safe.
"""
from threading import Event, Thread


def test_preempted_between_check_and_access():
    checked, resume, finished = Event(), Event(), Event()
    state = {"allowed": True, "power_off": False, "late_access": False}

    def worker():
        if not state["allowed"]:
            return
        checked.set()  # scheduler may preempt even after a fresh atomic check
        assert resume.wait(1)
        state["late_access"] = state["power_off"]
        finished.set()

    thread = Thread(target=worker)
    thread.start()
    assert checked.wait(1)
    state["allowed"] = False  # PM callback cancellation
    state["power_off"] = True  # PM budget expires and the OS advances
    resume.set()
    assert finished.wait(1)
    thread.join(1)
    assert state["late_access"], "the test must expose the check/access race"


def test_blocking_map_can_outlive_budget():
    entered, release, finished = Event(), Event(), Event()

    def fake_map():
        entered.set()
        assert release.wait(1)  # stands in for an API with no proven bound
        finished.set()

    thread = Thread(target=fake_map)
    thread.start()
    assert entered.wait(1)
    # Expiry/cancellation does not force an arbitrary synchronous API to return.
    assert not finished.is_set()
    release.set()
    thread.join(1)
    assert finished.is_set()


test_preempted_between_check_and_access()
test_blocking_map_can_outlive_budget()
print("PASS counterexamples reproduced; hard deadline remains NOT PROVEN")
