---
id: BUG-1307
title: civ_guest_editors / own_editor_civdrop compared game.mode's whole answer with the bare mode id, so the guest's open editor was never seen
status: open
kind: test
stage: Civ
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: civ_guest_editors, civ_guest_editors_off, own_editor_civdrop, own_editor_civdrop_off
signature:
docs:
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
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
