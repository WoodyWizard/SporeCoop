---
id: BUG-1413
title: Saves: a save the host asks for within about 1.5 s after a world-transfer save is dropped by the game and not retried
status: open
kind: bug
stage: All
reported: 2026-11-21
source: found on the way: BUG-1398 (lab 20261121-003202)
role: host
versions: 0.7.16
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Found on the way by fix/w62-saves (BUG-1398): the game's Save handler (0xB29960) silently drops a save that
comes while the previous one is not finished (BUG-1365). Since BUG-1365 (`world.save_allowed`) the mod refuses such a
save with its reason and retries it for the world transfer and the autosave, but a save the host player asks for
itself (the pause menu, a quit with save) within ~1.5 s after a world-transfer save for a joining guest is still lost
without a word. In the lab it hid in `twoinst.session_restart` (store writes 2 -> 2).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Planned for 0.7.16.1: retry (or delay) a refused player save until the game takes it, with a switch and a twin.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
