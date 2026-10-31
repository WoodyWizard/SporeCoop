---
id: BUG-0333
title: Creature: a guest's avatar is found ~14 m from where it joined, at the same spot (-159 -471 -86) in 2 cold team runs
status: open
kind: bug
stage: Creature
reported: 2026-10-30
source: fix/w17-creature, BUG-0330 analysis
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0330
---

## Report
In 2 of ~60 `team` runs in all lab history the guest's avatar stood ~14 m from where it joined, both
times at the same spot: out/lab/20261030-205830 (smoke, cold, all fixes on: guest trace (-158.5 -471.6
-86.4) 10 s after the load) and fix/w17-creature out/lab/20261030-230659 team#5 (cold,
`npc.kinematic` off: (-159.0 -471.4 -86.5); 0.9 m from the host before the joint hunt, 9.8 m before
the regen check). Both times the guest's own creature had been put on its avatar (SetSpecies,
"creations: own avatar is now 290225CD") a few seconds before.
Not the debug hit (a hit on proxies 14-37 m away did not move the guest), not the editor wait, not
npc.kinematic. Players would see their creature jump 14 m (inferred; no screenshot).
Reproduce: `tools/lab.py run team --repeat 10` cold and look for "players N m apart before the
regen check" with N > 5 (the team test now puts the guest back, it does not fail on it any more).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
