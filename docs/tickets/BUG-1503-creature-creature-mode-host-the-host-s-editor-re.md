---
id: BUG-1503
title: Creature (creature.mode host): the host's editor request opens before the stage-end and vote checks (CORE-4)
status: open
kind: bug
stage: Creature
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in: 
branch: fix/w80-net
commits: eb739ff7
tests: tools/tests/coop_stage_end_vote.py --host-mode (stage_end_vote_host_mode, twin stage_end_vote_host_mode_off)
signature:
docs:
related: 
switch: core.host_mode_vote
---

## Report
External static audit of 0.7.15.5, item CORE-4 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (vote.cpp the editor request): with creature.mode host the host's request opened at once
before the stage-end and running-vote checks (no "wants to move on to the Tribe stage" vote; a request during a vote opened
anyway), and a guest's request was refused also when the guest played its own world elsewhere (`host_world_now() != Same`):
it could not evolve there, in Cell its mate was forgotten.

## Fix
vote.cpp: `one_host` excludes the stage end and a running vote (they go the vote's way: start_vote / held), `one_guest`
excludes a guest outside the host's world (it evolves its own, as with creature.mode own). Switch `core.host_mode_vote`
(off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: stage_end_vote_host_mode, stage_end_vote_host_mode_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run stage_end_vote_host_mode --lane N --retry 0 --no-build` (creature.mode host: the host's stage end waits for the
  vote, the guest's window asks for I'm ready, the editor opens after the ready).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run stage_end_vote_host_mode_off --lane N --retry 0 --no-build` (the host's editor opens at once).
* Regressions near it: one_creature, stage_end_vote, editor_now, evo_screens_cell_one.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (eb739ff7); waiting for the night lab run.
