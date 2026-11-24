---
id: BUG-9371
title: Civ: a partner's camera starts on the host's city, not its own capital
status: open
kind: bug
stage: Civ
reported: 2026-11-24
source: deep playtest 2026-11-24
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tools/qa_win (manual, the deep playtest of 2026-11-24)
signature:
docs: docs/known-issues.md#known-civ-partner-camera-host-city
related:
---

## Report
Deep playtest 2026-11-24 (Civ, a galaxy planet's Civ era, a party of two): a partner entering the Civ era (the
crossing's arrival, and every later entry) starts with its camera on the host's city ("[Titan]"), its own capital
elsewhere on the planet (the blue marker on the minimap). Tribe starts at home (BUG-9152). Reproduce: two players
with a capital each, the partner enters the era; look where its camera stands. Not analysed.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-24: opened.
