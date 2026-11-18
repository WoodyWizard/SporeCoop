---
id: BUG-1245
title: Cell: the host's mate call while the vote holds: black screen without the vote window, the mating scene and egg laying repeat (6+ eggs)
status: open
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: host
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1239
---

## Report
A players' video (the guest had just called a mate and its editor opened at once). The host calls a mate: the host's screen fades to black for 25+ s ("where am I, I have no editor"), sometimes with no vote window over the black; every few seconds the black flickers and the cells mate and lay eggs again (6 eggs in a few seconds). The same twice more in the session.
A second analysis of the video suggests: vote.cpp's Submit hook holds the host's EditorRequest during the vote while the Cell stage has already started its fade and, with the mate still near, replays the mating scene and the egg laying (0xE78D10 -> Submit). Likely the same start as BUG-1239 (the guest in its own editor); the egg loop and the missing vote window are to be checked on 0.7.15 and with the BUG-1239 fix, also for a plain vote (the guest not in an editor).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
