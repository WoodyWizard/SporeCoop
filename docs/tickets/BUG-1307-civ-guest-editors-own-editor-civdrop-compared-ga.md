---
id: BUG-1307
title: civ_guest_editors / own_editor_civdrop compared game.mode's whole answer with the bare mode id, so the guest's open editor was never seen
status: fixed
kind: test
stage: Civ
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w57-own-editors
commits: 800a17e6 6b93ebd6 39aebbea 2cb5861c
tests: civ_guest_editors, civ_guest_editors_off, own_editor_civdrop, own_editor_civdrop_off
signature:
docs: docs/06-testing.md#testing-own-editors-night1
related: BUG-1275, BUG-1298, BUG-1276, BUG-1290
---

## Report
civ_guest_editors (lab 20261119-095414) failed "house design: the guest's own editor opened (mode 0x00DBDBA1 Editor;
BUG-1158 refused it in 0.7.15)" and own_editor_civdrop (20261119-104136) "the guest's own house editor opened
(0x00DBDBA1)": the failure line itself shows the guest in the editor. The guest's log has
"stage: Civilization -> Editor" two seconds after its request (civedit: our design editor request ...).

## Root cause
✔ (test code) coop_civ_guest_editors.py `mode()` returned the whole `game.mode` answer ("0x00DBDBA1 Editor", SporeKit
prints the id and the name) and compared it with the bare id '0x00DBDBA1': `in_editor` never saw an editor and
`drive_editor` (also used by civ_own_arrival, civ_late_arrival) returned at once as if the editor had closed. A bug in
the test, not in the mod. The real click on the tile's "new design" button had opened the editor; the test then
also sent `coop.civedit new` as its fallback route.

## Fix
`mode()` returns the first word of the answer (as harness.mode_word); the one check that needs the name
('GalaxyGameEntry') asks for the whole answer. No check was loosened: the same ids are compared.

## Verification
no-fail-run: a bug of the test (its check could never pass). Before: rel/0.7.16 out/lab/20261119-095414
civ_guest_editors and 20261119-104136 own_editor_civdrop failed "own editor opened (mode 0x00DBDBA1 ...)" with the
guest in the editor. After: fix/w57-own-editors worktree out/lab/20261119-111652 civ_guest_editors saw both editors open (house and vehicle);
fix/w57-own-editors worktree out/lab/20261119-130049 own_editor_civdrop PASS, civ_guest_editors passes every check but the host save's (BUG-1315 /
BUG-1326, not this ticket); civ_guest_editors_off still FAILs at the refusal ("house design: the guest's own editor
opened (mode 0x01654C04 ...)"). Later set-up fixes of the same tests: the house build (a bought NPC city has no house
of id 4E3F7777: 20261119-113002 found none) and the culture check, which now allows the entry the game itself makes for
that build with the culture's own design (20261119-122505: 4E3F7777 00000000 -> 0675F7B5, the culture's key) and still
fails on a changed entry or a guest key.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w57-own-editors, lab runs above.
- 2026-11-23: released in 0.7.16.
