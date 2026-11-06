---
id: BUG-0629
title: play_creature: a chat line typed in the co-op window and sent with the Send button reaches neither chat (1 of 3)
status: open
kind: bug
stage: Creature
reported: 2026-11-06
source: fix/w45-last play_creature out/lab/20261106-130130 #3
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
play_creature (fix/w45-last, out/lab/20261106-130130, play_creature#3, lane 4) FAIL "chat lines typed in the co-op window
reach both players (2/3)": the host's bot typed "host hunting 448" (coop.uistate showed it in the chat edit), clicked
Send; 1.5 s later neither game's chat (coop.status `chat:` lines) had it (mine ['* Bob joined', 'Bob: guest hunting
514'], theirs the same). The run's other checks passed. Not the BUG-0482 symptom (that one lost the typing). Not
analysed: whether the Send click missed (the window moved / closed by a dialog), or the line was sent and lost.
Reproduce: `tools/lab.py run play_creature --repeat 4` (1 of 4 runs here).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
