---
id: BUG-0283
title: Creature: the guest's own editor shows the host's new parts while it is open (DNA drops, cancel asks to confirm)
status: open
kind: bug
stage: creature
reported: 2026-10-29
source: fix/w7-legless test run 20261029-143204
role: guest
versions: main after 0.7.3.0 (fix/w7-finale dcff1cf)
fixed_in:
branch:
commits:
tests: tools/tests/coop_guest_editor_hold.py (suite guest_editor_hold, extended)
signature:
docs: docs/re/creations.md#creations-own-editor-save
related: BUG-0278
---

## Report
Seen once in `coop_legless_guest.py --guest cancel` (lab run 20261029-143204, lane 8, GOG 2024): the
guest's own editor (the vote's, on the host's legless pre-editor creature) opened showing the legless
creature with 115 DNA. The host added legs in its own editor and left it ("Alice evolved the species"
reached the guest 7 s after its editor opened). The guest's editor then showed the creature WITH the
host's legs and 65 DNA (the price of the legs), although the guest did nothing but click the red
cross. The cross then did nothing visible; a second click asked "Exiting will cause your changes to
be lost. Are you sure you want to exit?" (Cancel / Quit) and the guest stayed in its editor until it
answered (screenshots out/lane8/shots/legless_guest_editor_cancel.png, legless_guest_editor_stays_cancel.png,
legless_guest_after_cancel.png of that run; not kept in git).

To reproduce: `tools/lab.py run legless_guest_cancel --lane N` and look at the screenshots. Open
questions: whether the shared DNA total (BUG-0128) or the host's world package swap reloads the
creature in an open editor; whether a player can lose its own edits this way.

## Root cause
Not found. 2026-10-30 (fix/w9-creature, lane 9, 0.7.3.1 code): not reproduced in 3 runs of
`coop_guest_editor_hold.py` (the report's flow from the Cell stage): the red cross 3 s after the
guest's editor came up (out/lab/20261030-004649), and the editor left alone 5 s
(20261030-004334) and 40 s (20261030-005344) after the host's new creation and world reached the
guest: the editor kept the legless creature and 115 DNA, one click left it. In the report the first
click on the red cross did nothing; whatever kept the editor open may be what let the host's body
in. A first guess (the host's creation bundles registered while the editor is open) was tried as a
fix and dropped: the test passed without it too.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened (fix/w7-legless, while reproducing BUG-0278).
- 2026-10-30: not reproduced on fix/w9-creature (3 runs, coop_guest_editor_hold.py, suite extended).
