---
id: BUG-0463
title: sneak is not synced: a player in sneak mode is not shown as sneaking / invisible on the other screen
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
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
Both players marked it (s 2142, 2161). The audit: sneak (+0xB80 / +0xBB1, read by the hearing check 0xC0D6C0) is never sent or applied. Class G1/G2 (BUG-0418, BUG-0419); with BUG-0451 the sneak effect may relay but the state must too.
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
- 2026-11-02: opened.
