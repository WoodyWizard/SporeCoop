---
id: BUG-0700
title: Tribe / Creature: state of a mirror not applied after its spawn: a member's age, carried food; an NPC's befriended and stun state
status: open
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0419 (0.7.11)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0419
---

## Report
From the K2a audit of BUG-0419. A Tribe member's age is applied only at the mirror's spawn (a baby that grows on the host stays a baby on the guest); a member's carried food is read for the debug print only; an NPC proxy has no befriended flag and no stun state from the host.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
