---
id: BUG-0264
title: Guests fault on a freed avatar while leaving the creature stage for a shared world after the evolution (dc04aee5:coop.dll+0x82858, game::max_health)
status: open
kind: crash
stage: creature
reported: 2026-10-29
source: lab 20261029-070811 campaign
role: guest
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature: dc04aee5:coop.dll+0x82858
docs:
related:
---

## Report
Final 4-player campaign, lab run 20261029-070811, campaign#2 (lane 4, seed 958123806, run
out/lane4/campaign/20261029-070819). Cell -> Creature: the host evolved, every guest followed, then
the host shared its world a second time right after its creature editor. All three guests died
within a second of "world: leaving the stage to the galaxy menu before loading Cadian.spo":

* Bob (instance 8): `0xC0000005 at coop.dll+0x82858 (data 0x00000059)`, then a hang report
  (report-20261029-081902-i8.txt, copied into out/lab/20261029-070811/).
* Carol: `0xC000001D` at a heap address (a call into freed memory), then a freeze.
* Dave: `0xC0000005` at a heap address, then a freeze.

Bob's log, last lines: "Alice (host) is back" 25.2 s, "receiving world from Alice" 29.9 s, "leaving
the stage to the galaxy menu" 31.047, "save: 5 objects of ours are kept out of it" 31.111, the fault
32.105. The other two campaigns of the run passed the same change (lane 2 played on to Civ).

## Root cause
Observed: llvm-symbolizer on build/coop.dll (b4b747a): 0x10082858 = game::max_health (game.cpp
489, the vcall of cCombatant::GetMaxHitPoints), the return address on the stack 0x10088D33 =
modes::creature_fill (modes.cpp 43). creature_fill runs from local_state() every 50 ms with
g_local = game::avatar(), the noun manager's avatar field while the mode id is the creature stage.
The vtable read gave 0x01: the avatar object was freed or reset.

Inferred: after the quit to the galaxy menu is posted, the game saves and tears the stage down while
the mode id still reads Creature for about a second; the avatar field keeps the freed pointer.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
