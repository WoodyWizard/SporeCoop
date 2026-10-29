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
tests:
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
