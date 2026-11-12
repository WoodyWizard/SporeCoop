---
id: BUG-0952
title: Creature: a guest's NPC proxy whose herd the guest's own game destroyed (with the proxy's creature) is not made again for 9 s or more
status: fixed
kind: test
stage: Creature
reported: 2026-11-12
source: fix/w48-freed lab 20261112-012019, 20261112-013804 (npc_herd_identity)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w48-freed2
commits: 48082a85
tests: npc_herd_identity
signature:
docs: docs/06-testing.md#testing-freed2-w48
related: BUG-0843, BUG-0842
---

## Report
Seen by the BUG-0843 test (npc_herd_identity), with and without npc.herd_identity, in 4 runs: a host animal of a
species without a nest in the guest's world (06805133 in the creature fixture) has a proxy in a herd the guest made for
it (npc.cpp g_herds). The guest's game destroys that herd (`coop.npc reusehunt <herd> 150 xH`: DestroyInstance; the
proxy's creature goes with it), the guest's log says the proxy is made again (npc.proxy_identity), but `coop.npc list`
shows no spawned proxy of that id for 9 s and the log has no "proxy spawn failed" line (lab 20261112-012019 #5,
20261112-012736 #1, 20261112-013804 #3). For a player: such an animal would be missing on the guest until it is
made again, if the game ever removes such a herd by itself (not seen in play).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (fix/w48-freed2, lab 20261112-044259, 3 of 3 runs, with the diagnostics added to the test): the proxy the
test picked for the species without a nest (06805133) is a **body**: its `coop.npc list` line reads `pending ... DEAD`
with rx=10/s (the host sends states for the corpse). By BUG-0383's rule (npc.cpp guest_tick, `npc.dead_proxy`) a body
the guest's game made and removed (eaten, decayed, here destroyed with its herd) is not made again: the host's player
sees the body until its game removes it, the guest does not. That is the intended behaviour, not a lost animal; the test
picked a body because its filter (rx >= 5/s) did not exclude dead NPCs. Confidence: high.
Not known. Not checked: whether the host still sends states for that NPC (the test picked one with rx >= 5/s at
the start), whether the re-made proxy has no herd (the list prints herd=0 then: the parser would see it), whether the
host's NPC is flagged dead.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
tools/tests/coop_npc_herd_identity.py: bodies (the list's ` DEAD`) are no candidates. No mod change.

## Verification
* FAIL (the test before the change): out/lab/20261112-044259 (3 of 3: `#3 ... pending ... DEAD ... rx=10/s`).
* PASS: out/lab/20261112-044811 (3 of 3), out/lab/20261112-045126 (2 of 2, npc.herd_identity off: BUG-0843 stays
  no-fail-run, its stale entry is not asked).

## History
- 2026-11-12: opened.
- 2026-11-12: closed on fix/w48-freed2: a body, not made again by design (BUG-0383); the test picks no bodies.
