---
id: BUG-0968
title: Tribe: same_view_tribe view 1 shows Bob's chief (a guest-owned member) with its torch raised in the hand on the host and not on the guest
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-rest final check, same_view_tribe lab 20261112-145017
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
Key frame view1 of same_view_tribe in out/lab/20261112-145017 (fix/w48-rest, with tribe.mirror_hold_only on): the
host's game draws "Bob's chief" (the guest's chief, simulated by the host) with its torch raised in the hand, the
guest's copy of it stands with the torch in the resting grip. Alice's chief (the host's) holds its staff the same way
on both in all 4 views. The other direction of BUG-0962 (a host's hold the mirror lacks), or a still between the
host's hold start and the mirror's (0.0-0.07 s measured for the host's chief only). Seen once.

## Root cause
Not analysed. Next: tribe_hold_only for the guest's chief (the `missed` list of the test).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
