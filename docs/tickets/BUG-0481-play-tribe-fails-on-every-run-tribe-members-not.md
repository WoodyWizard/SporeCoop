---
id: BUG-0481
title: play_tribe fails on every run: tribe members not where the host has them (sync t*: 90% within 1.5 m), NPC proxies over 3 m off, the minimap view
status: open
kind: test
stage: Tribe
reported: 2026-11-03
source: nightly detector run 2026-11-03
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
play_tribe: 0 OK of 9 runs (6 before 2026-11-03, 3 in out/lab/20261103-012624). Failing checks: `sync t*: 90% within 1.5 m` (3/3), the proxies monitor rule (1 NPC proxy over 3 m off the host's NPC), `guest: the minimap takes the view where it points`. Either the Tribe member sync is worse than the limit under real input or the limit / measurement is wrong: measure which (what the guest sees) before changing the limit.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
