---
id: BUG-0952
title: Creature: a guest's NPC proxy whose herd the guest's own game destroyed (with the proxy's creature) is not made again for 9 s or more
status: open
kind: bug
stage: Creature
reported: 2026-11-12
source: fix/w48-freed lab 20261112-012019, 20261112-013804 (npc_herd_identity)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
Not known. Not checked: whether the host still sends states for that NPC (the test picked one with rx >= 5/s at
the start), whether the re-made proxy has no herd (the list prints herd=0 then: the parser would see it), whether the
host's NPC is flagged dead.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
