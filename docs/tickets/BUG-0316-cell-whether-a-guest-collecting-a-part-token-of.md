---
id: BUG-0316
title: Cell: whether a guest collecting a part token of the host's (a mirror) gets the mating-call button S+0x69 (split from BUG-0178 item 3)
status: open
kind: bug
stage: Cell
reported: 2026-10-30
source: BUG-0178
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0178
---

## Report
Split from BUG-0178 item 3: the guest's mating-call button needs a collected part (`S+0x69`,
docs/re/cell.md 12, set by 0xE5D7B0 when the own cell touches a part token). Not checked whether a
guest touching a part token the host's game dropped (a mirror on the guest) runs that path; the
tests set the flag by console (`coop.pool mate button`).
To check: Cell fixture, the host kills a cell that drops a part token (`coop.pool loot` lists the
host's drops), the guest swims onto its mirror (`coop.pool swimto c<key> tp`), `coop.pool mate` on
the guest (mate_button=1?). The fixture's view had no part token (2026-10-30, lane 5,
`coop.pool see 2000` on both games).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
