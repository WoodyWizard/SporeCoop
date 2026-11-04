---
id: BUG-0493
title: guest crash in Civ at SporeApp.exe+0x90C662 (null +0x2C) near cRelationshipManager::DeclareAlliance, 0.7.3.6
status: fixed
kind: crash
stage: Civ
reported: 2026-11-03
source: player report SporeCoop-report-20261103-194448.zip (owner downloaded 2026-11-03; 0.7.3.6, Steam guest, Civ), out/reports/2026-11-03
role: guest
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-civ-crashes
commits: 6db6dcbd, 9a9262ab
tests: civ_planner
signature: 3010a413:SporeApp.exe+0x90C662
docs: docs/re/civ.md#civ-planner-lifetime
related: BUG-0491, BUG-0426
---

## Report
Guest (Steam), Civ, 19:44:47, 1447 s into the session, 13 s after a partner's city was under attack: 0xC0000005 at 0x128C662 (3010a413 +0x90C662, read 0x2C), chain DeclareAlliance+0x12B0 < ? < UTFWin::Window::FindWindowByID < App::Canvas::func6Ch. Third crash of the same guest in 15 minutes (with BUG-0491 and BUG-0492 above): the Civ guest path has a lifetime problem (class G9).
Target: 0.7.4 (first: these are crashes on the current release).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (disassembly): 0xD0C662 is the planner's moved-objects update 0xD0C5F0 (called from the
  planner update 0xD124E0 through 0xD12599; DeclareAlliance+0x12B0 = 0xD07C10 is planner code too): it
  walks the map at editor+0x118 (objects the planner slides into place, vehicles included) and calls
  vt+0x2C on each object: a freed object (vtable 0) reads 0x2C. `busy: paused`: the planner was open;
  the guest's own city was under attack (its buildings ruined and rebuilt by the reconciliation, mirrors
  removed: `removed=` grew).
* Inferred: an object in that map (a building the reconciliation destroyed while it slid into its slot,
  or a mirror vehicle the guest removed) was freed by the mod under the open planner. The game itself
  changes nothing while the planner pauses it; the mod did.

## Fix
`civ.planner_hold`: see BUG-0491 (the city's buildings, owner and ruins are left to the snapshots after
the planner; a vehicle in the planner's moved-objects map is removed only when the planner lets it go:
`kills_held` / `kills_waiting` in the `planner:` line of `coop.civ`).

## Verification
* FAIL / PASS: as BUG-0491 (`civ_planner`, `out/lab/20261103-131021` and `out/lab/20261103-125816`).
* Not verified: the moved-objects map holding a mirror at the moment the host's vehicle dies (the map was
  empty in the lab: it fills when a building is dropped, which the test cannot drive).

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-civ-crashes (civ.planner_hold).
- 2026-11-03: released in 0.7.4.
