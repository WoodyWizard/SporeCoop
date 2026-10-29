---
id: BUG-0228
title: a "ship disabled" state that ends while the player is in an editor or an adventure leaves the ship invincible
status: open
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #162 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #162: https://github.com/WoodyWizard/SporeCoop/issues/162 (the full analysis, links and the scenario are there).

Since the BUG-0098 fix (30a25b0, release 0.7.3.0), ending the co-op "ship disabled" state clears the ship's invincible byte only while the game is in the Space mode. A session that ends while the player is in an editor or an adventure entered from Space leaves the byte at 1, because the Space game and the ship stay alive there, and the ship then stays invincible. Severity LOW, a small regression.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
