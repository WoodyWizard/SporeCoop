---
id: BUG-0234
title: Core: a cancelled evolution vote still opens every guest's own editor, and a pending own editor survives the session end
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #168 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-core
commits: c4f4981
tests: vote_end_editor, vote_drop_editor (coop_vote_drop_editor.py --case 1|2; --nofix fails), tests/validate_tests.cpp test_give_pick_leave
signature:
docs: docs/06-testing.md#testing-w5-core
related:
---

## Report
From GitHub issue #168: https://github.com/WoodyWizard/SporeCoop/issues/168 (the full analysis, links and the scenario are there).

**Summary**
When the host leaves the Creature stage during an evolution vote, the host cancels the vote (BUG-0043), but every guest still opens its own evolution editor, usually on the next frame. The guest cannot tell a cancelled vote from a passed one. Two more states are not reset when they should be:
- The guest's pending own editor (`g_own_editor_due`) outlives the session. A guest that leaves, or loses the host, just after a vote still gets its editor opened with no session.
- The Give recipient pick (`g_give_pick`) outlives its player and the session. It then points at whoever gets that id next.

Severity LOW-MED: an editor nobody asked for, while the host is in the galaxy menu. The BUG-0043 fix (fix/w1-core, 066df15) left this incomplete, and BUG-0033 (a387323) did not add these st

## Root cause
Confirmed on b423f56 by reading and in the game (lane 4, GOG 2024):
- The host's `drop_vote` resets `g_vote` and then broadcasts the end with `editor = 0`, but the guest's MSG_VOTE handler took the editor id only from "on" messages, so the end of a cancelled vote looked like a passed one and `g_own_editor_due` was set from the old id. Observed: the guest's game went to the editor 2.3 s after "cancelled (the stage was left)".
- `end_session_modules` did not reset `g_own_editor_due`, and `own_editor_tick` runs without a session: observed, the guest's editor opened 39.5 s after its Leave (when the test hold ended).
- `g_give_pick` was reset neither when its player left nor at the session end (read only).

## Fix
- Guest MSG_VOTE: the editor id is also taken from the message that ends a vote (`if (on || (ended && has_editor))`); a cancelled vote ends with 0 and opens nothing, `finish_vote` broadcasts before it resets, so a passed vote still ends with its id. `drop_vote` has a comment that its `editor = 0` is on purpose. Switch `coop.fix core.vote_drop_editor`.
- `end_session_modules` clears `g_own_editor_due` (a task already queued with `run_outside_frame` reads it first and returns; `g_own_editor_queued` is left alone). Switch `core.own_editor_end`.
- `g_give_pick` is cleared when its player leaves (`net::give_pick_after_leave`) and at the session end. Switch `give.pick_reset`.
- Test hook `coop.vote hold <s>`: a due own editor waits that long (so a Leave can land before it opens).
- Mixed versions: an old guest with a new host keeps the bug; a new guest with an old host behaves as before (old hosts never cancel a vote).

## Verification
`tools/tests/coop_vote_drop_editor.py` (lane 4, GOG 2024; oracle: the guest's `game.mode`):
- `--case 1` (a passed vote, the guest leaves while its own editor is due): `--nofix` FAIL (the guest's game in the editor `0x00DBDBA1` after the Leave, log "own editor opened (39.5 s after the vote)"), PASS with the fix (Creature for 55 s).
- `--case 2` (cancelled vote, the host goes to the galaxy menu): `--nofix` FAIL (guest in the editor 2.3 s after the cancel), PASS with the fix.
Run logs: scratchpad w5/fix_w5-core vde_nofix2.log (both FAIL in one run), vde_fix_c1.log, vde_fix_c2.log; lab runs in the branch report.
- Give pick: `validate_tests` test_give_pick_leave (fails with `--nofix`). The 3-player game test of the issue was not written (3 games do not fit the memory budget of this wave).

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w5-core): fixed: the editor id from the end message, the due editor and the Give pick end with the session / their player; coop_vote_drop_editor.py case 1 and 2 FAIL with --nofix, PASS with the fix.
