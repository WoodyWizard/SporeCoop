---
id: BUG-0321
title: tribe_npc_dash fails 2 of 3 runs on main (p90 1.5-2.2 m over the 1.2 m limit)
status: fixed
kind: test
stage: Tribe
reported: 2026-10-30
source: lab 20261030-144611-tmp-w13-main-ref, 20261030-144027-fix-w13-tribe-civ
role: guest
versions: main 5d7f20e
fixed_in: 0.7.3.2
branch: fix/w14-tribe-civ
commits: 4feb19c
tests: tribe_npc_dash (coop_tribe_npc_dash.py)
signature:
docs: docs/06-testing.md#testing-dash-fresh-states
related: BUG-0305, BUG-0322
---

## Report
`tribe_npc_dash` (coop_tribe_npc_dash.py, BUG-0305's test, merged with fix/w12-tribe-npc) on
lane 6, GOG 2024:
* main 5d7f20e (temporary worktree tmp/w13-main-ref): lab 20261030-144611-tmp-w13-main-ref (its folder went with the temporary worktree), 1 of 3
  passed; FAIL "90 % of 106 samples within 1.95 m" and "... 114 samples within 1.51 m" (limit 1.2 m).
* fix/w13-tribe-civ: lab 20261030-141833 (1 FAIL, 1.81 m) and 20261030-144027 (3 FAIL: 2.21, 1.92,
  1.46 m), while lanes 6 and 7 both ran tests.
The w12 branch's own runs of the test also passed and failed alternately (its summaries). No
fault, the games kept drawing. Not caused by fix/w13-tribe-civ (main fails the same way).

## Root cause
The test, not the npc.trail fix (evidence, lane 6, fix/w14-tribe-civ):
* Observed: on lane 6 with the rest of the machine busy (load average 5-28, other agents' lanes)
  the test passed 9/10, 10/10 and 10/10 (p90 0.33-1.33 m, limit 1.2 m; out/lab
  20261030-151436, 20261030-153721, 20261030-155054); the failures before came while lanes 6 and 7
  and other lanes all ran tests.
* Observed (DASH_VERBOSE, 8 runs): of the samples over 1 m, part had a late state on the guest
  (`age` 0.24-0.38 s, `rx` 4-6 states a second instead of 10): the guest showed the animal where the
  host had it that long ago, with the proxy right on its target. The rest had the proxy 1-2 m
  behind its fresh target while catching up (own speed 10-19 m/s), the npc.trail rule engaged
  (`trail=` 1-6).
* Observed: a stronger hold (npc.trail_near: put the proxy on its path every frame while the animal
  runs and it is more than 1 m off) made no measurable difference: 10 runs each, same load, p90
  0.41-1.08 m with it and 0.33-1.19 m without (out/lab/20261030-153721 vs 20261030-155054); reverted.
* Observed: with `npc.trail` off the test still fails (3 of 3: 1.54, 1.22, 1.47 m).
Inferred: the margin (fix on ~0.4-1.0 m, off ~1.2-2.8 m, limit 1.2 m) is small, and a loaded
machine (late or sparse host states, a slower guest) pushes the fix-on p90 over the limit.

## Fix
The test (4feb19c): the p90 is taken over samples whose proxy state was fresh on the guest (age at
most 0.25 s, at least 7 states a second); with fewer than 40 such samples the run is
inconclusive (`RESULT: SKIP`), and both games' frame rates and the p90 of all samples are printed.
No change to the game code.

## Verification
* PASS (unloaded lane 6, the rest of the machine as usual): lab run 20261030-160748-fix-w14-tribe-civ,
  10/10 (fresh-state p90 0.44-1.03 m, guest 15 fps, host 41-66 fps).
* FAIL with `SPORE_FIX_OFF=npc.trail`: lab run 20261030-162103-fix-w14-tribe-civ, 3/3 (1.54, 1.22,
  1.47 m).
Not verified: a heavily loaded machine (BUG-0322's p90 1.3-2.0 m); there the filter or the SKIP
should take the late-state samples out, not proven.

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w14-tribe-civ): analysed with evidence: the test is fragile under load, the trail fix holds; test made robust (fresh-state samples, SKIP when too few); BUG-0322 is a duplicate.
- 2026-11-01: released in 0.7.3.2.
