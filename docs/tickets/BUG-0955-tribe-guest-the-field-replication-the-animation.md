---
id: BUG-0955
title: Tribe guest: the field replication, the animation relay and the effects relay still use a mirror's freed citizen (object_alive), BUG-0844's fix missed them
status: open
kind: crash
stage: Tribe
reported: 2026-11-12
source: rel/0.7.13 lab 20261112-041619-2, 20261112-031338 (tribe_mirror_lifetime)
role: guest
versions: 0.7.12 .. rel/0.7.13
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
