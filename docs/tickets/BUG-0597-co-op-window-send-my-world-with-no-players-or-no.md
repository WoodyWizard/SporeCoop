---
id: BUG-0597
title: Co-op window: 'Send my world' with no players or no world loaded does nothing but says 'Sending the world to everyone'
status: open
kind: bug
stage: Menu
reported: 2026-11-05
source: owner's manual test 2026-11-05 (0.7.7)
role: host
versions: 0.7.7
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The host hosts from the main menu (Galaxy menu, 1/4 players) and clicks "Send my world": nothing happens, while
the window says "Sending the world to everyone...". There is no world loaded and nobody to send it to
(coop_ui.cpp share button calls A.share_world() unconditionally). Players read it as "solo does not start".
Expected: the button is greyed out or says why ("Load or start a game first" / "Nobody has joined yet"), and
the lobby says that a host can just start or load a game and friends can join any time.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
