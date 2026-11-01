---
id: BUG-0324
title: play_civ: the end audit finds a ruin (and a building) on the guest that the host no longer has
status: fixed
kind: desync
stage: Civ
reported: 2026-10-30
source: play_civ.py --seed 11 / --seed 9, fix/w13-tribe-civ and fix/w14-tribe-civ
role: guest
versions: main 4dbacc5
fixed_in: 0.7.3.2
branch: fix/w14-tribe-civ
commits: 9689722
tests: civ_ruin_repair (coop_civ_ruin_repair.py)
signature:
docs: docs/06-testing.md#testing-dash-fresh-states
related:
switch: civ.ruin_repair
---

## Report
`play_civ.py --minutes 10` on lane 7 (2026-10-30): the end audit (the only one that reports ruins
and building counts as problems, once, without a second look) failed with
* seed 11 (fix/w14-tribe-civ, out/w14/play_civ_11.log of the worktree): "city 1: 0 ruins on the host,
  1 on the guest", "city 1: 1 buildings on the host, 2 on the guest", "city 3: 0 ruins on the host, 1
  on the guest";
* seed 9 (fix/w13-tribe-civ, out/w13/play_civ9_a.log): "city 2: 0 ruins on the host, 1 on the guest".
During the session the audits count such differences (`ruins_differ`, `buildings_differ`) without
failing; the guest's `buildings_fixed=+N/-M` shows the reconciliation correcting buildings.

## Root cause
Observed (code): `reconcile_buildings` (civ.cpp, the guest's per-city reconciliation with the
host's building list) only turned a building into a ruin when the host's was one; a guest building
that was a ruin while the host's was whole stayed matched and was never undone.
Inferred (not seen directly): after a siege that ended without a capture the host's game rebuilt
a ruined building (city 1 of seed 9 had capture max 19, not taken); the guest's own game did not.
Observed in play_civ: 3 of 6 end audits on lane 7 had such a city.

## Fix
`civ.ruin_repair` (9689722): a ruin on the guest whose host building is whole does not match it; it
goes as an extra (DestroyBuilding) and the host's comes back as a missing one (build_in_city), the
reconciliation's existing paths (`buildings_fixed=+N/-N`).

## Verification
`coop_civ_ruin_repair.py` (the guest's game alone ruins one building of an NPC city,
`coop.civ ruinlocal`): FAIL with `SPORE_FIX_OFF=civ.ruin_repair`: lab run
20261030-162619-fix-w14-tribe-civ (the ruin stayed 20 s); PASS: lab run
20261030-162734-fix-w14-tribe-civ (2 runs, the guest matched the host within 2 s, buildings_fixed +1/-1).
Not verified: the host's rebuilding after a siege itself (the stand-in makes the guest's state).

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w14-tribe-civ): fixed (civ.ruin_repair), stand-in test FAIL 20261030-162619, PASS 20261030-162734.
- 2026-11-01: released in 0.7.3.2.
