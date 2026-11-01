---
id: BUG-0422
title: class: per-player state under a reused id, and state that existed before a proxy (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
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
Fixed one module at a time: BUG-0031, 0061, 0064, 0076, 0079, 0109, 0125, 0166, 0234; BUG-0383 dead-first proxies; BUG-0370 newest 24 species.
Siblings not covered: leave fan-out (coop.cpp:3173-3194) misses spacecoop, rivalry, tribe, civ, squadron, space_ext; Space g_last_star / g_partner_down (Jump-to goes to the departed player's star, spacecoop.cpp:1156/1323/:402/:2097) (obs); Rivals g_budget credited to a newcomer (space_rivalry.cpp:67) (inf); guest drops MSG_NPC_STATE for unknown ids and never asks for the spawn (BUG-0328) (obs); late joiner inherits the host's once-per-save flags (inf); g_last_regen (teamplay.cpp:18) and g_last_down (spacecoop) not reset per session (obs); species profile cache keeps the host creature after a leave.
General fix: one per-player registry with join/leave observers keyed by a join serial (or no id reuse in a session); receivers request a resync of unknown ids (rate-limited); per-session reset registry. Outcome check: 3-game id-reuse test; dropped-spawn recovery test.
Source: docs/generalisation-plan.md G5.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
