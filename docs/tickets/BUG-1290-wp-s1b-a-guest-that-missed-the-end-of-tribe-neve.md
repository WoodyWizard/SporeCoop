---
id: BUG-1290
title: WP-S1b: a guest that missed the end of Tribe never gets its own City Hall and vehicle editors in Civ
status: fixed
kind: bug
stage: Civ
reported: 2026-11-19
source: plan
role: guest
versions: 0.7.15.2, rel/0.7.16
fixed_in: 0.7.16
branch: feat/w56-late
commits: c29e3ba4 4b9fe0de
tests: civ_late_arrival, civ_late_arrival_off (twin)
signature:
docs: docs/re/civ.md#civ-late-arrival, docs/06-testing.md#testing-late-joiners
related: BUG-1276, BUG-1275, BUG-1274, BUG-1289
switch: core.own_civ_late
---

## Report
Plan item WP-S1b (out/research/lineages-species.md, ranked item 4): a guest that missed the end of Tribe (it joins
the host's Civ world, reconnects or restarts after the Tribe -> Civ change) never gets its own City Hall and vehicle
editors: BUG-1276 opens them only after the guest saw the host's end-of-Tribe vote end in the host's Tribe.

## Root cause
Observed in code: civ_arrival.cpp makes the editors due only in civ_arrival_vote (session_glue's vote end in Tribe);
the phase is a session state, so a later join or a new session never has them due.

## Fix
`core.own_civ_late` (civ_arrival.cpp, creations.cpp world_note): the first arrival of a session in the host's Civ
world is looked at once (30 s grace for the host's families): due when the arrival path is allowed (own mode, a host
with the line, WP-C3's editors), the vote path is not due, the guest plays an own creature here, its line has no own
City Hall nor any own Civ design, and its SporeCoop\worlds.ini has no `[civ_arrival] <world crc>` note. Then the same
settle and editors as BUG-1276; a stored row that brings an own City Hall before the settle ends drops it. The note is
written when the City Hall editor opens (on the vote path too; the red cross counts), so a later join of that world
offers nothing. A guest that plays the host's species at its arrival gets none (BUG-1289's row first; the editors at
its next arrival). Off: only the vote path (0.7.15 .. BUG-1276).

## Verification
- PASS: civ_late_arrival in fix/w58-wineserver worktree out/lab/20261119-165546, to its end: the first arrival offers
  none, after the rejoin the guest's own City Hall (23 s after the join) and vehicle editors (51 s) opened and were
  driven by real clicks, both designs in its line on both games, its city's City Hall and citizens its own on both
  games (key frames 01-04 looked at), the host's culture set unchanged, and the third join offers nothing (why
  'offered in this world already'). Also fix/w57-own-editors out/lab/20261119-130049, -142554 up to the third join.
- FAIL (twin): civ_late_arrival_off in out/lab/20261119-165546 at "the guest's OWN City Hall editor opened" (301 s,
  late 'core.own_civ_late is off'), as in fix/w57-own-editors out/lab/20261119-130049.

Earlier (before the lab): not run yet: the orchestrator holds the lab. Test `civ_late_arrival` (civ_npc: the start's arrival offers nothing, the
guest chooses its creature, restarts and joins: its own editors by real clicks, both designs in its line on both
games, its bought city's City Hall drawn from its design and its citizens its species on both games, the host's
culture set unchanged, a third join offers nothing) and the twin `civ_late_arrival_off` (must FAIL at "the guest's OWN
City Hall editor opened"). `make -j8` and `make test` pass. Run dirs: pending.

## History
- 2026-11-19: opened.
- 2026-11-19: code, test and twin on feat/w56-late; lab pending.
- 2026-11-19: fix/w57-own-editors: civ_late_arrival: the late arrival's own editors PASS (fix/w57-own-editors worktree out/lab/20261119-130049, -142554: first arrival offers none, after the rejoin both editors opened, both designs in the line, City Hall and citizens on both games); twins civ_late_arrival_off, _families_off (BUG-1306), _unpause_off (BUG-1323) FAIL in -130049. The test's last step (a third join offers nothing) is not reached: the host's game stops answering when the guest leaves the second time (BUG-1327, analysed by fix/w57-own-editors). Open until that step runs.
- 2026-11-19: fix/w58-wineserver: with BUG-1327 fixed (lane games share the wineserver's PID namespace) civ_late_arrival PASSES to its last step (20261119-165546), the twin FAILS where it should (same run). Fixed.
- 2026-11-19: on the tree merged with rel/0.7.16 (1d5f3532): civ_late_arrival PASS, civ_late_arrival_off FAIL at the City Hall editor (out/lab/20261119-171000).
- 2026-11-23: released in 0.7.16.
