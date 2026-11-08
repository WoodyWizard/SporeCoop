---
id: BUG-0753
title: creature: an NPC proxy's interpolation target 10-12 m from the host's running NPC for seconds with fresh states (creature_npc_dash)
status: fixed
kind: test
stage: Creature
reported: 2026-11-08
source: fix/w46-g4-tests lab 20261108-082630
role: guest
versions: 0.7.11 (rel/0.7.11 + fix/w46-g4-tests)
fixed_in: unreleased
branch: fix/w46-k4-proxy
commits: 6b9d124f, 735cff8b
tests: creature_npc_dash, tribe_npc_dash
signature:
docs: docs/06-testing.md#testing-dash-order
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
Observed (the rows of lab 20261108-092241 of fix/w46-g4-tests, n2 dashing, n3 its mate): on the host n3 stayed 6.8-9.4 m
from n2 for the whole run (it moved under 0.13 m a sample) while on the guest it ran 1.2 m beside n2, where `coop.npc dash`
puts the mate. The test's dash_tick (set_position of both animals) ran in the host's npc tick before the NPC states were
built, so the states carried the test's own write; the host's game keeps that write for the dashing NPC but not for the
mate (its next update puts it back), so the guest showed the mate where the test wanted it and the comparison with the
host's own NPC was 3.6-12 m. A test artefact: the guest showed exactly the states it got; in play no mod write precedes
the states. G4's three inferred candidates (smoothing, drive.lead_fall, the list probe) are excluded by their runs.

## Fix
`coop.fix tools.dash_after_send` (npc.cpp, tests only): dash_tick runs after the states went out, so the states are the
game's own positions as in play, and a dashing NPC whose written position the game kept carries the dash's velocity (the
game's own velocity after its update is its locomotion's: with it alone tribe_npc_dash got 3.6-3.9 m, lab
20261108-101527-2). Off: before the send (the test as it was).

## Verification
- FAIL: out/lab/20261108-095739 (SPORE_FIX_OFF=tools.dash_after_send, creature_npc_dash 1 of 3 FAIL: 90 % within 9.54 m, limit
  1.2 m); G4's 20261108-091147 / -091630 / -092113 before (2 of 3 each).
- PASS: creature_npc_dash out/lab/20261108-095739-2 (3 of 3, the same time on lane 6) and out/lab/20261108-102113-2 (3 of 3, the
  final build).
- tribe_npc_dash (out/lab/20261108-102113): 1 of 3; its target is right now (p90 0.55-0.63 m, before 1.0-3.2 m), the rest is
  the driving one guest frame behind its target at 15 fps, a separate ticket: BUG-0757.

## History
- 2026-11-08: opened (fix/w46-g4-dash, static analysis of lab 20261108-082630).
- 2026-11-08 (fix/w46-g4-tests): G4 diagnostic runs (lane 2, creature_npc_dash x3 each): out/lab/20261108-091147 (2/3), with SPORE_FIX_OFF=drive.lead_fall 20261108-091630 (2/3), DASH_FEED=0 20261108-092113 (2/3). Every failing run has n3 among the two dashing animals and n3 off by a constant 3.6-8.1 m for the whole run (raw lead = target = 3.6 / 8.1 m from the host's NPC, driving ~0.1 m); every run with the pair n1/n2 passes (p90 0.5-0.6 m). Not the smoothing (raw lead M1 is as far), not drive.lead_fall (the same with it off), not the test's coop.npc list probe (the same with DASH_FEED=0). Inferred: the host's states for that NPC id are not the host NPC's own position (a wrong object or offset behind the id), or the guest maps that id to another position; the earlier independent sync-check 'n3 10.9 apart' agrees. Needs a dedicated fix (K4 class: remote bodies).
- 2026-11-08 (fix/w46-k4-proxy): cause found (the test's set_position before the state send, the mate not kept by the host's game); fixed in the test path (tools.dash_after_send).
