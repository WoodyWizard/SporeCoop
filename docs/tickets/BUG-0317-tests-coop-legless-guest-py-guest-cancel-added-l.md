---
id: BUG-0317
title: tests: coop_legless_guest.py --guest cancel added legs and pressed the save dialog's Cancel, not the red cross
status: fixed
kind: test
stage: Creature
reported: 2026-10-30
source: fix/w13-creature, BUG-0283
role: 
versions: 
fixed_in: 0.7.3.2
branch: fix/w13-creature
commits: 3922e14
tests: tools/tests/coop_legless_guest.py --guest cancel (suite legless_guest_cancel)
signature:
docs:
related: BUG-0283, BUG-0318
---

## Report
`coop_legless_guest.py --guest cancel` is documented as "the red cross", but `SAVE_DIALOG` had a key
`cancel` (the save dialog's Cancel, 612 604), so the test added legs on the guest, clicked done and
then the dialog's Cancel; its red-cross branch was dead code. Its screenshots led to BUG-0283.

## Root cause
Observed in the test source since ac3f711 (`if how in SAVE_DIALOG: add_legs(guest)` before `elif how == 'cancel'`).

## Fix
The key `cancel` is gone from `SAVE_DIALOG`; `--guest cancel` now clicks the red cross only and
checks that one click leaves the unchanged editor (waiting up to 12 s: the editor takes ~4 s to close).

## Verification
no-fail-run: a test's own input. Lab 20261030-141448 ran the red cross for the first time
(its check, still at 3 s, failed on the closing editor; the guest log shows Editor -> Creature 4 s
after the click without a dialog). With the 12 s wait: PASS, out/lab/20261030-142629 ("one click on
the red cross leaves the unchanged editor"). It can still fail later for BUG-0318.

## History
- 2026-10-30: opened.
- 2026-10-30: found and fixed on fix/w13-creature.
- 2026-11-01: released in 0.7.3.2.
