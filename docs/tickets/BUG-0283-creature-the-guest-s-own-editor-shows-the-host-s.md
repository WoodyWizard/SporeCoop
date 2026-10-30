---
id: BUG-0283
title: Creature: the guest's own editor shows the host's new parts while it is open (DNA drops, cancel asks to confirm)
status: not-reproduced
kind: bug
stage: creature
reported: 2026-10-29
source: fix/w7-legless test run 20261029-143204
role: guest
versions: main after 0.7.3.0 (fix/w7-finale dcff1cf)
fixed_in:
branch: fix/w13-creature
commits: 3922e14
tests: tools/tests/coop_guest_editor_hold.py (suite guest_editor_hold, extended)
signature:
docs: docs/re/creations.md#creations-own-editor-save
related: BUG-0278, BUG-0317, BUG-0318
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
Not a bug of the mod or the game: the report came from the test's own input. In
`coop_legless_guest.py` (since its first version, ac3f711) the dialog table `SAVE_DIALOG` had a key
`cancel` (the save dialog's Cancel button), so `--guest cancel` took the `how in SAVE_DIALOG` branch:
it **added legs on the guest** (`add_legs(guest)`), clicked done and then the save dialog's Cancel;
the red-cross branch (`elif how == 'cancel'`) never ran. The legs and the 115 -> 65 DNA in the
report's screenshots were the guest's own edit, the editor stayed open because the dialog's Cancel
keeps it open, and the second click (the test's fallback: red cross, Quit) asked "your changes will
be lost" because there were changes. Seen again on lane 4 (2026-10-30, the old test): the same
screenshots, the legs on the guest's own creature right after `add_legs(guest)`.
With the test fixed (BUG-0317) the red cross leaves the unchanged editor without a dialog (lab
20261030-141448: the editor closed 4 s after the click; the check then looked after 3 s and called
it a dialog, now it waits 12 s; with that, out/lab/20261030-142629 passes the check). 5 runs of `coop_guest_editor_hold.py` (3 on fix/w9-creature, 1
here) never showed a change either.

## Fix
None needed in the mod. The test: BUG-0317.

## Verification
no-fail-run: not a bug (the test's own edit, see Root cause).

## History
- 2026-10-29: opened (fix/w7-legless, while reproducing BUG-0278).
- 2026-10-30: not reproduced on fix/w9-creature (3 runs, coop_guest_editor_hold.py, suite extended).
- 2026-10-30: fix/w13-creature: explained: the test pressed add_legs + the dialog Cancel for --guest cancel (BUG-0317); not-reproduced.
