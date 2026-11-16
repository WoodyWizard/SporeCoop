---
id: BUG-1199
title: editor_after_swap / editor_after_load click a red cross the guest's evolution editor has not had since BUG-0720 (0.7.11)
status: open
kind: test
stage: Creature
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Final check 20261116-025154: editor_after_swap and editor_after_load FAIL "the red cross brings the guest back to the
Creature stage" (the editor opened and drew). Reproduced on lane 10: rel/0.7.15 (20261116-082820) and v0.7.14.1
(fix/w51-t2-v07141/out/lab/20261116-084702), both tests.

## Root cause
Observed (lane 10 shot of the guest's editor, 20261116-082820): the editor shows only its green check, no red cross.
The test opens it with `coop.vote editor`, which builds the request through `build_editor_request(EditKind::Edit)`:
since BUG-0720 (0.7.11) that request carries the game's evolution flags (+0x39 / +0x3B = 0, +0x3C = 1), and an
evolution editor has no red cross (docs/re/creations.md#creations-editor-request-builder; only the own editor a vote
opens keeps the constructor's values and the cross, BUG-0756). The tests' last PASS runs (20261108-084617,
20261031-205913) are older than 0.7.11; neither test ran on rel or main between 0.7.11 and this final check. An old
failure of the test, not of the mod; not a 0.7.15 change.

## Fix
coop_editor_after_swap.py leaves the editor with its green check (the way out of an evolution editor), then Quit if a
box asks; the check is the same (the guest is back in the Creature stage within 30 s / 50 s). BUG-0318's black editor
still fails it (an editor that does not draw takes no click).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
