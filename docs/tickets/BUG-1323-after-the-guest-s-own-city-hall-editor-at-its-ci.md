---
id: BUG-1323
title: After the guest's own City Hall editor at its Civ arrival its game stayed paused (Civ's editor return pauses 0x4BF38A7 and resumes it only through the planner): the vehicle editor never opened
status: fixed
kind: bug
stage: Civ
reported: 2026-11-19
source: lab
role: guest
versions: rel/0.7.16
fixed_in: 0.7.16
branch: fix/w57-own-editors
commits: 39aebbea 5e4f14e1
tests: civ_own_arrival, civ_late_arrival, line_stages_editors
signature: GameCiv_Update 0x00CFE2C0 Pause(0x4BF38A7)
docs: docs/re/civ.md#civ-editor-return-pause
related: BUG-1276, BUG-1290, BUG-1306, BUG-1277
switch: core.own_civ_unpause
---

## Report
Lab 20261119-113002 (fix/w57-own-editors, after BUG-1306): civ_own_arrival and civ_late_arrival got the guest's own City
Hall editor, the guest left it with a creation, then "the guest's OWN vehicle editor opened after its arrival in Civ"
failed (phase next for 300 s). The guest's game was `busy=paused` from the editor's return to the end, no HUD
(screenshot civ_arrival_no_1.png). In line_stages_editors the same guest then bought no city and no vehicle.

## Root cause
✔ (runtime pause trace + Ghidra, docs/re/civ.md#civ-editor-return-pause) GameCiv_Update 0xCFE2C0 pauses 0x4BF38A7 while
the editor-return state machine 0xCFBD70 runs; the machine resumes it only when the mode before was not the editor,
because after an editor the re-opened planner carries that pause. Our arrival editors come from no planner, so the
pause stayed (`coop.pauses`: 04BF38A7=1) and civ_arrival's settle (which waits for a running game) never ended.

## Fix
`core.own_civ_unpause` (civ_arrival.cpp): when our own editor came back to Civ and cGameModeCiv +0xB4 reached 10
(`SporeCoop::GameCiv_Get`), a 0x4BF38A7 counted more than when our editor was requested is resumed once (log "the Civ
return left the game paused ... resumed (BUG-1323)", `coop.civarrival`: `unpause: ... resumed=N`). Off: the game stays
paused (the first lab run).

## Verification
FAIL: fix/w57-own-editors worktree out/lab/20261119-113002 civ_own_arrival, civ_late_arrival ("the guest's OWN vehicle editor opened ... phase
next"), line_stages_editors; twin on the merged tree fix/w57-own-editors worktree out/lab/20261119-130049 civ_late_arrival_unpause_off FAIL the same
("phase next ... late due"). PASS: probe on lane 2 (`unpause: ... resumed=2`, both editors, busy empty); lab
fix/w57-own-editors worktree out/lab/20261119-122505 and -130049: civ_own_arrival and civ_late_arrival open the vehicle editor 5 s after the City Hall
one ("the guest's OWN vehicle editor opened after it came back ... (51 s after the join)"), line_stages_editors gets
both designs.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w57-own-editors, lab runs above.
- 2026-11-23: released in 0.7.16.
