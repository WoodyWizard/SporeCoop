---
id: BUG-0998
title: Cell: parts.shared off also stops the Cell parts sharing (parts.shared_cell), and the queued Cell collect survives the session end
status: open
kind: bug
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-16
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0401, BUG-0467
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-16. Not seen at runtime, found by code reading. parts::on_message returns at
once when `parts.shared` (BUG-0401, the Creature parts) is off, before the branch for the Cell parts kinds 3 and 4
(`parts.shared_cell`, BUG-0467). With `coop.fix parts.shared off` (tests, the lab's fix-off twins) the Cell parts are not
shared either, so a Cell test with that switch off sees a second, unrelated change. Also the queue of Cell part
collects waiting for the next outside-frame call (`g_cell_queued`) is not cleared in on_session_end; cell_collect_now
checks only that the game is in Cell, not that a session is on.

## Root cause
Observed in code (main efd2e0ba):
* mods/coop/parts.cpp:458-461 on_message: `if (type != net::MSG_PARTS || !g_fix) return;` before the Cell branch at
  474-499 (which checks its own `g_cell_fix`). tick (446-457) runs cell_tick independent of g_fix, so the host still
  sends kind 3 sets: only the receiving side is cut.
* parts.cpp:316-334 cell_collect_now (outside the frame) and 341-353 cell_queue; on_session_end (527-544) resets the Cell
  fields except `g_cell_queued` / `g_cell_queued_from`.
Inferred (high confidence): only reachable with a coop.fix switch off (players do not switch parts.shared); the stale
queue lasts until the next outside-frame run (the same or next frame), so at most a part queued in the last frame of a
session is collected after it. Severity: low (tests and fix-off twins).

## Fix idea
Move the `!g_fix` check below the Cell branch (Creature kinds only); clear `g_cell_queued` in on_session_end and check
`C.sess && connected` in cell_collect_now. No switch needed for the reorder (a test-only interplay); the queue reset can
go under the lifecycle reset package's shared mechanism.

## How to test
tools/tests/coop_cell_parts.py with `SPORE_FIX_OFF=parts.shared`: the guest collects a Cell part and the host must get
it (the game's own Cell part state, `coop.parts cell` reads the game's array). FAIL on main (the host never gets it),
PASS with the reorder.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
