---
id: BUG-0954
title: Test world_queue part 4 fails since BUG-0508: the host's evolution goes in place for a guest in its editor, no world is sent for the BUG-0074 check
status: open
kind: test
stage: Creature
reported: 2026-11-12
source: lab (fix/w48-saves area run)
role: 
versions: 
fixed_in:
branch:
commits:
tests: world_queue
signature:
docs:
related: BUG-0508, BUG-0074
---

## Report
Lab 20261112-024053 (fix/w48-saves, lane 3): world_queue part 4 FAIL "the host's world waits for the guest to
leave its editor" and "the guest stays in its Creature game after leaving the session". The last passing runs are of
2026-10-30, before BUG-0508. Reproduce: `tools/lab.py run world_queue`.

## Root cause
Observed in the host log: after the host's editor "evolved in place: Bob keeps its world" (BUG-0508: a guest in its
own editor counts as in the same world), so no world is sent and the BUG-0074 check (a received world waits while
the guest is in its editor, then is dropped at its Leave) never gets a world. Inferred: a stale test, not a bug of the
mod; the test needs a share that is not in place (for example `SPORE_FIX_OFF=world.evolve_in_place` inside the test
for that part, or a guest in another world). Not caused by fix/w48-saves (the in-place decision is untouched).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
