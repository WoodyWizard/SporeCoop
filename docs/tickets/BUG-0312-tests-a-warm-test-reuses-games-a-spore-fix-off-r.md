---
id: BUG-0312
title: tests: a warm test reuses games a SPORE_FIX_OFF run left with the fix switched off
status: fixed
kind: test
stage: all
reported: 2026-10-30
source: fix/w13-creature BUG-0281 runs, lane 5
role: 
versions: 
fixed_in: 0.7.3.2
branch: fix/w13-creature
commits: df245fa
tests: tools/tests/twoinst.py
signature:
docs:
related:
---

## Report
Lane 5, 2026-10-30: a run with `SPORE_FIX_OFF=npc.trail` (cold), then two runs of the same test
with `SPORE_WARM=1` and no switch off. The warm runs reused the games and the fix was still off in
both (`coop.fix list` on both games: `npc.trail off`): their "fix on" results were fix-off results.
The lab runs warm tests the same way, so a twin FAIL run followed by a warm PASS run on the same lane
could pass or fail for the wrong reason.

## Root cause
Observed: `coop.fix` switches live in the game process; `twoinst.start` switched them off after
start and nothing switched them on again; the warm reuse checked only build, fixture and profile.

## Fix
`twoinst.start` notes `fix_off` in the lane state when it switches fixes off; `_start` does not
reuse such games (a cold start writes a fresh lane state).

## Verification
no-fail-run: test harness only; the stale state was seen with `coop.fix list` on lane 5 (no lab run
keeps it); after the change the lab run right after a SPORE_FIX_OFF run (out/lab/20261030-140802) started cold: out/lab/20261030-140958 ("both games ready 50 s", lane 5).

## History
- 2026-10-30: opened.
- 2026-10-30: fixed on fix/w13-creature.
- 2026-11-01: released in 0.7.3.2.
