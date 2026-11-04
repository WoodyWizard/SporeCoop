---
id: BUG-0530
title: civ: a guest shows religious (cultural) shots and AoE areas the host's game does not make (noun_only_guest after BUG-0477)
status: open
kind: desync
stage: Civ
reported: 2026-11-04
source: divergence detector, play_civ (fix/w41-civ-combat, out/lab/20261104-024331)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0477
---

## Report
After BUG-0477's shot relay (civ.shots) the detector in play_civ (out/lab/20261104-024331) has
`Civilization:noun_only_guest:cultural_projectile` and `noun_only_guest:default_ao_e_area` in 29 % of the
comparisons (the host's whole list has none, the guest's one or more), and in the regression run before the last
change 16 % (out/lab/20261104-020931). The military shots match (civ_shots: 98 guest / 102 host projectiles).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed. Inferred: the host's game calls FireTool for a religious vehicle (and an AoE tool) every update while
its target is in range but its weapon makes a projectile / area only now and then (or the area lives on the
host's side shorter); the guest calls FireTool for the mirror every 0.1 s and its weapon's own refire time lets
more through. Next: relay only the FireTool calls that made a projectile on the host (WhileFiring's return, or the
new projectile in the noun list), or skip cultural / AoE tools (tool +0x178 strategy key) until then.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
