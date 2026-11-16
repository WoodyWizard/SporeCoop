---
id: BUG-1166
title: Tests: editor_desync fails 'the guest gets the vote with an I'm ready button' since 0.7.12 (the check's fixed button rectangle)
status: open
kind: test
stage: Cell
reported: 2026-11-15
source: B1 impact run 20261115-133237
role: 
versions: 0.7.12, 0.7.13, 0.7.14, 0.7.14.1, rel/0.7.15
fixed_in:
branch: feat/w51-npc
commits:
tests: editor_desync
signature:
docs: docs/06-testing.md#testing-w51-npc
related: BUG-0765, BUG-1138
---

## Report
The B1 impact run (feat/w51-retire 20261115-034753, rerun 20261115-133237 lane 3) and rel/0.7.15 5206247b
(20261115-141538) failed editor_desync at "the guest gets the vote with an I'm ready button"; the next check (the host's
editor opens after the vote) passed, so the vote and the guest's click worked.

## Root cause
Observed: the guest's log has the vote (`vote: started by the host`), `ui: co-op dialog ready` 10 ms later and the
test's click (`ui: click ready`, `own ready=1 set (vote on)`) 0.6 s after: the button was there. The check compared the
whole `coop.uistate` line with the fixed rectangle `ready 380 728 580 768 1 'I'm ready'`; F1 of 0.7.12 (ea0f5a5b, BUG-0765:
the window in steps, kH 644 -> 660) moved the window's rows by 8 px, so the string never matched again (the click at
(478, 747) still lands inside the new rectangle). The last PASS of editor_desync on any branch is 2026-11-08
(20261108-044355), before F1 reached the branches that ran it. Not a regression of 0.7.15: v0.7.14.1 fails the same
(lane 3, 20261115-154901 of the tag's worktree).

## Fix
tools/tests/coop_editor_desync.py: the button is found by name and caption (`READY_BTN`, any rectangle), waited for up
to 10 s, and clicked at its centre. Test-only.

## Verification
FAIL (the old check) on v0.7.14.1 lane 3 and rel/0.7.15 lane 2: 20261115-154901 (both trees).
PASS: (pending)

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-npc): root cause (the stale rectangle), test fixed.
