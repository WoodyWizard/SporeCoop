---
id: BUG-0224
title: with guests in the session the host's cinematics are cut off after 8 seconds, not only the capture caption
status: open
kind: bug
stage: Civ
reported: 2026-10-28
source: GitHub #158 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #158: https://github.com/WoodyWizard/SporeCoop/issues/158 (the full analysis, links and the scenario are there).

**Summary**
The BUG-0187 fix ends the host's cinematic in Civ after 8 s whenever 2 or more players are in the session, so that a capture caption waiting for OK does not stop everybody's game. It ends any cinematic of the game's cinematic manager, not only that caption. Other Civ cinematics, such as the start of a city purchase, the camera cinematic when an NPC city appears, or superweapon cinematics, are cut short on the host, and a cinematic that ends a game step may leave that step half done. Severity: low (wrong behaviour). Introduced by f0c5f0a (BUG-0187) in release 0.7.3.0.

**Where**
- `movie_playing()` is true for every cinematic (cCinematicManager +0x2C): https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/mods/coop/civ.cpp#L1060-L1064
- `movie_ti

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
