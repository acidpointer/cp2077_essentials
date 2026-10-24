"""Small interleaving model; design evidence, not a runtime concurrency test."""
import json
from itertools import combinations

def schedules(left, right):
    for places in combinations(range(len(left) + len(right)), len(left)):
        places = set(places)
        a = iter(left)
        b = iter(right)
        yield [next(a) if i in places else next(b) for i in range(len(left) + len(right))]

def run(schedule):
    alive, mapped, active, admitted = True, True, 0, False
    bad = False
    for step in schedule:
        if step == "check":
            admitted = alive
        elif step == "increment":
            if admitted:
                active += 1
        elif step == "enter":
            # Models one successful compare-exchange on {alive, active}, not
            # two atomics with a check/increment window between them.
            admitted = alive
            if admitted:
                active += 1
        elif step == "invoke":
            bad |= admitted and not mapped
        elif step == "leave":
            if admitted:
                active -= 1
        elif step == "close":
            alive = False
        elif step == "unmap_if_idle":
            if not alive and active == 0:
                mapped = False
        else:
            raise AssertionError(step)
        assert active >= 0
    return bad

naive = list(schedules(["check", "increment", "invoke", "leave"], ["close", "unmap_if_idle"]))
counterexamples = [s for s in naive if run(s)]
safe = list(schedules(["enter", "invoke", "leave"], ["close", "unmap_if_idle"]))
assert counterexamples
assert not any(run(s) for s in safe)

# An idle domain can still be reachable from an engine-owned callback slot.
active, retained_slots = 0, 1
assert active == 0 and retained_slots != 0
can_unload = lambda alive, active, slots, owners: not alive and active == slots == owners == 0
assert not can_unload(False, 0, 1, 0)
assert not can_unload(False, 0, 0, 1)
assert can_unload(False, 0, 0, 0)

# Guest address reuse defeats an internal generation counter if stale raw
# guest pointers carry no generation. The original view must not be reused.
native_address = 0x1000
old_guest, new_guest = 0x2000, 0x3000
objects = {(native_address, 1): old_guest, (native_address, 2): new_guest}
retired = {old_guest}
assert objects[(native_address, 1)] != objects[(native_address, 2)]
assert old_guest in retired and new_guest not in retired

print(json.dumps({
    "naive_schedules": len(naive), "naive_unsafe": len(counterexamples),
    "counterexample": counterexamples[0],
    "atomic_enter_schedules": len(safe), "atomic_enter_unsafe": 0,
    "retained_callback_blocks_unload": True,
    "native_address_reuse_requires_distinct_guest_address": True,
    "limits": "Two actors, one active caller, no weak-memory model; reclamation liveness and runtime atomics untested"
}, indent=2))
