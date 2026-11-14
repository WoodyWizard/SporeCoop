---
id: BUG-1039
title: Space: a guest's colony spice values of an older world (or of Rivals) are taken as a collection after a world load in the session
status: fixed
kind: desync
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-14
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-money
commits: d500f3b0
tests: space_spice_rivals
signature:
docs: docs/re/space.md#space-rival-merge-known, docs/known-issues.md#issue-colony-spice-old-world
related: BUG-0822, BUG-0840
---

## Report
Code audit for 0.7.14, finding SPC-14. Not seen at runtime, found by code reading.
The guest's last-known spice of each shared colony (space_planets g_gc) is kept for the whole session. After the guest loads another world in the session (a rejoin without a session end, the host loading another save of the same galaxy) or after a Rivals period, the first host K_SPICE makes the guest compare its new world's values with the old ones; lower values count as "this game collected" and K_SPICE_TAKEN writes them into the host's colonies: the host's colony spice drops to the older world's amounts.

## Root cause
Observed in code (main efd2e0ba), confidence medium (the paths are certain, how often the values differ by more than 0.5 is inferred):
* space_planets.cpp:253 g_gc, cleared only in on_session_end (:362-366); no world_epoch tag.
* space_planets.cpp:306 guest_state calls guest_check(now, true) before writing the host's values; guest_check (:257-289) sends K_SPICE_TAKEN for any city whose value is 0.5 below g.last.
* active() (:129-132) is false in Rivals, so g_gc is frozen at the values before Rivals.
Audit detail: the guest sends its current values, not necessarily 0.

## Fix idea
Tag g_gc with spaceext::world_epoch() (and the Rivals switch): another epoch or a Rivals period clears it, so the first K_SPICE after it only writes. Switch: `coop.fix space.colony_spice_epoch off`.

## How to test
Extend coop_space_colony_spice.py: sync once, the host lets the colony grow, the guest reloads the host's world in the session (share again) so its values are older and lower, then the first K_SPICE; assert the host's game colony value did not drop. Main: FAIL.

## Fix
space_planets.cpp, switch `coop.fix space.colony_spice_epoch` (off: 0.7.13): gc_fresh() (guest tick and
guest_check) clears the last-known values g_gc when spaceext::world_epoch() changed or while Rivals is on, so the
first host K_SPICE after another world or a Rivals period only writes; no K_SPICE_TAKEN of older values. Status
`gc_dropped=`.

## Verification
`coop_space_colony_spice.py --case rivals` (suite `space_spice_rivals`, fixture space): the home colony synced at 3
per city, a Rivals period with the guest's game at 0.5 and the host's at 4.5, back to Allies: the host's colony must
keep 4.5 and the guest's game get it.
* FAIL (SPORE_FIX_OFF=space.colony_spice_epoch): out/lab/20261113-005801 (the host's colony dropped to 0.5 per city;
  the guest sent taken_tx=1)
* PASS: out/lab/20261113-010104, out/lab/20261113-010344 (4.5 on both games, gc_dropped=1, taken_tx=0)
The other path of the ticket (a reload of the host's world in the session) is covered by the same epoch rule and was
not run separately.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-money (d500f3b0): g_gc tied to the world epoch and cleared by Rivals; FAIL/PASS in the lab.
- 2026-11-14: released in 0.7.14.
