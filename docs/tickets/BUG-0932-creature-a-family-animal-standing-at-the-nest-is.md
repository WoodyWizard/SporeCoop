---
id: BUG-0932
title: Creature: a family animal standing at the nest is 8.4 m from its copy on the guest in two sync-checks 3 s apart (long_creature, seen once)
status: not-reproduced
kind: desync
stage: Creature
reported: 2026-11-11
source: fix/w47-final12 lab 20261111-071518 long_creature (lane 3), 14 min in
role: guest
versions: 0.7.12
fixed_in:
branch: fix/w48-rest
commits:
tests: creature_far_family, creature_far_family_host
signature:
docs: docs/06-testing.md#testing-w48-rest
related: BUG-0550, BUG-0753, BUG-0402
---

## Report
Lab 20261111-071518 long_creature (lane 3), 14 min into the session, the lab's sync-check gate:
`sync-check: MISMATCH with player 1 'Bob' (Creature, 2 checks 3 s apart, 10 objects compared): n: 1 apart (n1 8.4)`.
The host's line: `n1 here: #1 species=065CBE70 hp=10.00 dist=22.7 pos=(-164.8 -469.9 -84.0) spd=0.0 ... family`:
an animal of the host's family standing still near the nest; the guest was walking towards the nest from 80 m.

## Root cause
Not found; not reproduced (2026-11-12, fix/w48-rest). The guest's log of that run starts after 07:29 (it rolled over:
its side of n1 is not kept), so only the host's line is known: n1 stood (spd 0, moved64 0) 22.7 m from the host's
creature. Attempts:
* creature_far_family (lab out/lab/20261112-142823): the guest walks 64-88 m from the nest and back
  4 times (its own walk, `coop.npc gopos`), the host's creature at the nest; every 0.5 s each standing family NPC of the
  host against its copy's place in the guest's game (`at=`): worst 0.4 m in 655 samples.
* The same with the host's creature 23 m from the nest (the report's distance): worst 0.4 m in 638 samples.
* All long_creature host logs on this machine of 2026-11-10 .. -12 (32 runs): one sync-check MISMATCH naming a family
  animal, this one.
Was: Not in the two later long_creature runs of the branch (out/lab/20261111-080711, -080715) nor in six
long_creature_short runs.

## Fix
None (not reproduced). The probe stays in the suite (`creature_far_family`, `creature_far_family_host`) as a check of
the case.

## Verification
no-fail-run: not reproduced. Probe runs: out/lab/20261112-142823, out/lab/20261112-144539 (--host-off 23): no
standing family animal 3 m or more from its copy (worst 0.4 m).
How to look next time: the guest's game log of the run (it must cover the moment), `coop.npc list` of the NPC on both.

## History
- 2026-11-11: opened.
- 2026-11-12: not reproduced in two probes (1293 samples, worst 0.4 m) and in 32 long_creature runs; closed (fix/w48-rest).
