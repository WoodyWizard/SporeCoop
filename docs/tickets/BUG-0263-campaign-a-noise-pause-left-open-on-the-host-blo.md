---
id: BUG-0263
title: campaign: a noise pause left open on the host blocks the stage change (History clicks do nothing)
status: open
kind: test
stage: Creature
reported: 2026-10-29
source: fix/w5-snapshot, lane 3 campaign run 20261029-060328
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
`SPORE_LANE=3 python3 tools/tests/coop_campaign.py --from creature --to tribe --minutes 3 --advance-minutes 10`
(seed 67500682, run out/lane3/campaign/20261029-060328 of fix/w5-snapshot): the play phase ended
with "Alice noise_pause=1"; at the advance the host's pause menu (Options) was still open, so the
History clicks of kick_creature did nothing and the stage change timed out after 10 min (finding
`stuck`, the screenshot shows the Options box). The same run with `--noise 0` reached Tribe in
125 s. Likely fix: close the pause menu ("Resume Game") at the start of advance() and before each
kick on the host.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
