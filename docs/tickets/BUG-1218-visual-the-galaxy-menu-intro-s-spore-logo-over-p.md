---
id: BUG-1218
title: Visual: the galaxy menu intro's SPORE logo over play on the host after a quick load from the menu (menu_splash, civ_designs key frames)
status: open
kind: bug
stage: Menu
reported: 2026-11-16
source: plan
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
Key frame review of the 0.7.15 final check (lab 20261116-025154): the host's frame of menu_splash (S/062, Creature play
with the HUD, the status block, "Bob appeared" and the arrow over a black starfield with the big glowing SPORE logo) and
of civ_designs (S/035, the Civ HUD over a white glow with the SPORE logo and letterbox bars). Both frames were shot
0.3-0.9 s before the host's log line "world: the galaxy menu intro would start outside the menu (mode ...): not started
(BUG-0626)", ~16 s after its galaxy menu came up; both hosts had loaded their world from the menu ~4 s after it came
up (the lab's quick start, A9). The question: a real overlay a player sees, or a loading frame shot too early?

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
