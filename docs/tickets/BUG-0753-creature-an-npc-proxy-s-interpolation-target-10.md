---
id: BUG-0753
title: creature: an NPC proxy's interpolation target 10-12 m from the host's running NPC for seconds with fresh states (creature_npc_dash)
status: open
kind: desync
stage: Creature
reported: 2026-11-08
source: fix/w46-g4-tests lab 20261108-082630
role: guest
versions: 0.7.11 (rel/0.7.11 + fix/w46-g4-tests)
fixed_in:
branch:
commits:
tests: creature_npc_dash, tribe_npc_dash
signature:
docs:
related: BUG-0363, BUG-0674, BUG-0281
---

## Report
creature_npc_dash judged at the same moment (BUG-0363's measure, fix/w46-g4-dash), lab 20261108-082630 (lane 2,
host 55-70 fps, guest 15.4-15.7 fps, every window sample with a fresh state, rx 9-10/s): p90 4.91 / 11.35 / 2.87 m,
limit 1.2 m. In each run ONE of the two dashing nest NPCs (065CBE70) is metres off for seconds, the other is fine:
* #1: n1 p90 8.25 m (n2 1.25), 7-10.7 m off at 31.6-34.8 s;
* #2: n3 p90 11.51 m (n1 0.57), 10.5-12 m off from 28 to 39 s; the guest's own sync-check logged
  `MISMATCH with the host ... n: 1 apart (n3 10.9)` at the same time (independent of the test);
* #3: n2 p90 4.67 m (n1 0.54), 4.7-4.8 m at 7.1-7.4 s, 2.5 m at 12.3 s.
The split of the rows: the guest's interpolation target (q<id> rows, the target the driving used) is as far off as the
drawn proxy (#2: target 11.1-12.0 m, driving 0.00-0.33 m; driving p90 0.15-0.25 m in all three): the proxy goes
where it is told; the target itself is 10 m from the host's NPC. The console readings of the old window measure
agree at the same instants (10.38 m window / 10.32 m same moment), and the copy is not on the host's path at any
time within 0.6 s (lag search at its limit), so it is not a late copy of the dash.
Tribe (tribe_npc_dash, same lab): 1.36 / 1.65 / 1.68 m, split half target (p90 ~0.66) and half driving (~0.67).

How to reproduce: `tools/lab.py run creature_npc_dash --repeat 3 --retry 0` (fails 3 of 3 on 20261108-082630).

## Root cause
Not found. Observed: the error is in the target, with fresh states. Candidates (inferred, not checked):
* drive.lead_fall / the NPC proxies' smoothing (BUG-0674, 34c2d19c today: `px.buf.smooth_max = 8.f` for NPC
  proxies, the lead's jumps up to 8 m kept as a decaying offset, the continuation at the shown velocity): an offset
  that does not decay or a continuation that runs on the wrong way at a dash's turn would put the target metres off
  with fresh states. Split it with the `u<id>` rows (M1's raw lead, no late time, no smoothing: added to
  `coop.bodyrec start diag` on fix/w46-g4-dash) and with `SPORE_FIX_OFF=drive.lead_fall`.
* the console's `coop.npc list` (the test's fresh-state probe, every 0.6 s) calls `px.buf.sample()` at the console's
  own time between frames, and sample() moves the lead's state (late_time, smoothing: mutable members): the test may
  disturb what it measures. Check with `DASH_FEED=0` (no list during the dash).
* states of another creature under the same id on the host (one NPC of the two, never both).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened (fix/w46-g4-dash, static analysis of lab 20261108-082630).
- 2026-11-08 (fix/w46-g4-tests): G4 diagnostic runs (lane 2, creature_npc_dash x3 each): out/lab/20261108-091147 (2/3), with SPORE_FIX_OFF=drive.lead_fall 20261108-091630 (2/3), DASH_FEED=0 20261108-092113 (2/3). Every failing run has n3 among the two dashing animals and n3 off by a constant 3.6-8.1 m for the whole run (raw lead = target = 3.6 / 8.1 m from the host's NPC, driving ~0.1 m); every run with the pair n1/n2 passes (p90 0.5-0.6 m). Not the smoothing (raw lead M1 is as far), not drive.lead_fall (the same with it off), not the test's coop.npc list probe (the same with DASH_FEED=0). Inferred: the host's states for that NPC id are not the host NPC's own position (a wrong object or offset behind the id), or the guest maps that id to another position; the earlier independent sync-check 'n3 10.9 apart' agrees. Needs a dedicated fix (K4 class: remote bodies).
