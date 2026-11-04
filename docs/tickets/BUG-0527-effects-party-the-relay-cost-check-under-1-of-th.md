---
id: BUG-0527
title: effects_party: the relay cost check (under 1 % of the frame time) fails on a loaded machine in Tribe / Civ, also with the 0.7.4 build
status: open
kind: test
stage: Tribe
reported: 2026-11-04
source: lab, fix/w41-effects-stages, lane 4, 2026-11-04
role: 
versions: 
fixed_in:
branch:
commits:
tests: effects_party_tribe, effects_party_civ
signature:
docs:
related: BUG-0451, BUG-0471
---

## Report

coop_effects_party.py --fixture tribe|civ checks that each of the three games spends under 1 % of its frame time
in the relay while all three load 2.5 effects a second. On lane 4 (machine load average 8-11 on 6 cores, other
lanes running) it failed with 1.0-5.4 % (out/lab/20261104-005534, -014354, -004532, -003023, -003205), and the
0.7.4 release build itself failed it the same way (1.27-2.28 %, two runs with its own test). No fault.

## Root cause

Observed: the hooks cost 0.26-3.5 us per created effect (0.0001-0.001 % of the main thread); the share comes
from `effects::tick` with single frames of 38-165 ms (`perf: effects ... tick max`), which includes the test
workload's own game calls (`coop.effects load`: the game's CreateEffect / Start) and the Release of relayed
effects. Inferred: those spikes are the game's work and preemption under the machine load, not the relay; the
check counts them as relay cost.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification

Open. To decide: leave the workload's game calls out of the measured time, or use the median frame instead of
the mean.

## History
- 2026-11-04: opened.
