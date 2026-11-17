---
id: BUG-1199
title: editor_after_swap / editor_after_load click a red cross the guest's evolution editor has not had since BUG-0720 (0.7.11)
status: fixed
kind: test
stage: Creature
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-t2
commits: bf914c98, a678425c, 5db495f3
tests: editor_after_load, editor_after_swap
signature:
docs: docs/06-testing.md#testing-creature-after-worlds-reset, docs/re/creations.md#creations-own-mating-editor
related: BUG-0720, BUG-0756, BUG-0318, BUG-1222
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
* FAIL (the red cross): out/lab/20261116-025154 (rel/0.7.15 worktree, final check); out/lab/20261116-082820 (lane 10, both tests); v0.7.14.1
  fix/w51-t2-v07141/out/lab/20261116-084702 (lane 10, both tests).
* PASS: editor_after_load (green check) out/lab/20261116-090728, 20261116-094534, 20261116-095548; editor_after_swap
  with BUG-1222 out/lab/20261116-095548; final out/lab/20261116-100349 (lane 10, --repeat 2: both copies PASS).

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t2 (runs above).
