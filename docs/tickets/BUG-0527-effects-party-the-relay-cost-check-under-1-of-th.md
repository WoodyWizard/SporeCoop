---
id: BUG-0527
title: effects_party: the relay cost check (under 1 % of the frame time) fails on a loaded machine in Tribe / Civ, also with the 0.7.4 build
status: fixed
kind: test
stage: Tribe
reported: 2026-11-04
source: lab, fix/w41-effects-stages, lane 4, 2026-11-04
role: 
versions: 
fixed_in: 0.7.6
branch: fix/w42-infra
commits: 7b452d83
tests: tools/tests/coop_effects_party.py (suite effects_party, effects_party_tribe, effects_party_civ)
signature:
docs: docs/06-testing.md#testing-effects-cost
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

Observed 2026-11-04 (fix/w42-infra, `effects cost parts` line, lane 4 at load average 8-12): (1) the test workload's own CreateEffect calls (`load`) are 0.2-3.2 % of the main thread, inside the old measurement; (2) the relay ticks over 5 ms (29-35 per 20 s at load 10-12, 5-822 ms each) held 2.6-5.3 % of the wall time but 0-5 jiffies of thread CPU time (GetThreadTimes, build of lab 20261104-122037): preemption; (3) the relay's own cost over the frames under 5 ms is 0.42-0.58 %, mostly live_tick (0.25-0.56 %). Also observed: the old number (`tick avg` x frames) restarts every 60 s with the perf line, so a window across it was wrong.

## Fix
mods/coop/effects.cpp: `coop.effects` prints `effects cost parts:` (wall ms of load / rx / live / rest, the relay part = the tick without load_tick, split into frames under 5 ms and slow frames). The test checks hooks + relay over frames under 5 ms < 1 % and slow frames < 5 % of the frames (a relay slow in every frame still fails). `--old-cost` runs the old check. The limit is unchanged. Measurement only, no game behaviour, so no COOP_FIX switch. Tried and dropped: the thread's CPU time (10 ms steps under Wine, ~50 us a call: 0.4 % of the main thread when called every frame).

## Verification
FAIL (old check, harness reason): SPORE_LANE=4 coop_effects_party.py --fixture tribe --old-cost, 2026-11-04 12:27: 2.28 / 2.58 / 3.00 % (worktree out/effects_party_tribe_oldcost_20261104.log), and the runs of the report. PASS: lab 20261104-123812 (lane 4, load average 8-11): effects_party, effects_party_tribe, effects_party_civ, relay 0.42-0.58 %, slow frames 0-1.03 %; in the same run the old number was 1.27 / 1.21 % (Tribe) and 1.00 % (Civ). Also lab 20261104-122037 (load 10-12: old number 5.2-8.9 %, all passed with the CPU variant of the check).

## History
- 2026-11-04: opened.
- 2026-11-04: fixed on fix/w42-infra: the check measures the relay (parts line), not the workload and preemption.
- 2026-11-04: released in 0.7.6.
