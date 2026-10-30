---
id: BUG-0318
title: Creature: a guest's own editor opened 3 s after it loaded the host's world stays black and never closes
status: open
kind: bug
stage: Creature
reported: 2026-10-30
source: fix/w13-creature lab 20261030-141448 legless_guest_cancel
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0317, BUG-0278
---

## Report
`coop_legless_guest.py --guest cancel` (lab 20261030-141448, lane 4, and one direct run before it):
after the guest's rejoin (Creature -> galaxy menu -> load -> Creature at 15:17:41) the test calls
`coop.vote editor` on the guest 2 s later; its own editor opened at 15:17:44 (stage line "Creature
-> Editor") and stayed in mode Editor for the rest of the test (5 min): the screen black (no editor
UI, screenshot legless_guest_next_editor_cancel.png), "nothing controlled", 13-17 fps, hitches of
200-360 ms. The test's checks "the guest saves its edit" and those after it fail.
Reproduce: `tools/lab.py run legless_guest_cancel --lane N`: 2 of 3 runs on 2026-10-30 (lane 4; the third,
out/lab/20261030-142629, opened and saved the next editor normally).
Not known: whether it needs the rejoin right before (a world loaded 3 s earlier) or also happens in
`--guest new`; whether a player can leave such an editor (Esc, the red cross) at all.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
- 2026-10-30: opened on fix/w13-creature (not analysed, outside the batch time).
