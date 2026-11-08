---
id: BUG-0754
title: Creature: sync-check MISMATCH n 4 apart (21-28 m) while the host saves for a world share (792 ms hitch), campaign_ct
status: open
kind: test
stage: Creature
reported: 2026-11-08
source: fix/w46-k4-ground lab 20261108-085348-2 #2
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
campaign_ct#2 of lab 20261108-085348-2 (fix/w46-k4-ground 6cd28ceb on rel/0.7.11 fa4f5df3, lane 6) failed the sync-check
gate: `sync-check: MISMATCH with the host (Creature 9652F8AD, 18 objects compared): n: 4 apart (n8 27.6 n1 26.4 n6 24.2
n5 21.4)` at s 293.9 on the guest and the host. At that moment the host was saving for a world share to the three guests
("world: saving for the share of player 1", `perf: hitch 792 ms (Creature, saving)` at s 292.6) and the guest logged
"receiving world from Alice" at s 293.4; the guest's proxy n8 was 0.89 m from its target (the copy followed the host's
states). Inferred: the check compared positions across the host's save hitch / share (BUG-0385's family), not a drawing
error. Not reproduced in the other 5 campaign_ct runs of the same build. Reproduce: tools/lab.py run campaign_ct --repeat 3.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
