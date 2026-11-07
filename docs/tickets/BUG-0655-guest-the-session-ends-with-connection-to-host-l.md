---
id: BUG-0655
title: Guest: the session ends with 'connection to host lost (timeout)' after 29.5 s silence while the host keeps running
status: open
kind: bug
stage: All
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
5 runs on 4 branches: `net: the session with the host ended: connection to host lost (timeout)` 29.5 s after the host was last heard while the host keeps running; `no answer from host (timeout)` with rtt -1 on host_bundle. Examples: rel/0.7.8 out/lab/20261105-123757/long_creature_short.guest.game.log:139; fix/w45-world-objects out/lab/20261106-042445/host_bundle.guest.game.log:83; fix/w45-crashes out/lab/20261105-225531/cell_pause_rejoin.guest.game.log. Not yet checked whether the host stalled. Related: BUG-0030, BUG-0104, BUG-0138.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
