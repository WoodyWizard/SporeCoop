---
id: BUG-0972
title: Tribe: same_view_tribe view 1: the host's avatar (Alice) crouches at the fire on the host while the guest's puppet of it stands about 1 m off in a stepping pose and Bob's chief differs in pose
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-last same_view_tribe lab 20261112-172240 (#1)
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
Key frame 01_view1 of same_view_tribe-1 in out/lab/20261112-172240 (fix/w48-last, a54a52bf): on the host, the host's
own creature "Alice" crouches next to the fire and Bob's chief stands bent forward; on the guest, its puppet of Alice
stands about 1 m to the left in a stepping pose and Bob's chief is mid-step with a leg raised. Views 1 of the other
two runs (-2, -3) agree. The test's creature match passed (positions within its tolerance). Seen once; it may be an
animation phase at the moment of the still (the avatar's puppet is driven by the creature sync, not the member relay).
Reproduce: `lab.py run same_view_tribe --repeat 3` and compare 01_view1 pairs.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
