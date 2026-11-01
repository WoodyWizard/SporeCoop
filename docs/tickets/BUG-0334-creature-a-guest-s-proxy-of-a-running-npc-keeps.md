---
id: BUG-0334
title: Creature: a guest's proxy of a running NPC keeps its old health and does not die while it runs (npc.kinematic skipped the state sync)
status: fixed
kind: desync
stage: Creature
reported: 2026-10-31
source: final smoke lab 20261030-235253 team
role: guest
versions: main 0b76599 (0.7.3.2 candidate)
fixed_in: 0.7.3.2
branch: fix/w18-creature
commits: 9d9cc68
tests: npc_run_hp (tools/tests/coop_npc_run_hp.py), team
signature:
docs: docs/re/creature.md#creature-kinematic-sync
related: BUG-0281, BUG-0330
---

## Report
Third final verification smoke (out/lab/20261030-235253, lane 2, cold) failed `team`: "the guest's
mirror shows the health after the joint hit (4.2 / host 1.95)". Seen before on fix/w17-creature
(out/lab/20261030-231732 team#2, warm): the same numbers. The hit NPC ran off (host list: spd=4.0).

## Root cause
Observed in the code and the test: a regression of BUG-0281's `npc.kinematic` (fix/w14-creature,
aaa98ac). For a moving host NPC it teleports the guest's proxy onto the host's path and then went on
to the next proxy (`continue`), so the rest of the per-proxy step (health `set_health`, the death
replay on `NF_DEAD`, the impressed flag) did not run while the NPC moved. An NPC that flees after a
joint hit kept its old health on the guest until it stopped, and an NPC killed while running stayed
alive on the guest until then. Not a test timing issue (the test waits 5 s for the health).

## Fix
`npc.kinematic_sync` (npc.cpp): the kinematic placement replaces only the driving; health, death and
the impressed flag are applied for running NPCs too. `coop.fix npc.kinematic_sync off`: the first
version.

## Verification
`coop_npc_run_hp.py` (suite npc_run_hp): a host NPC dashes (40 m legs, 4 m/s); hit while it runs, then
killed while it runs; the guest's game must show the health within 3 s and the death within 3 s.
* FAIL with `SPORE_FIX_OFF=npc.kinematic_sync`: out/lab/20261031-020145 (6.0 / host 4.0; no death).
* PASS: out/lab/20261031-020245 (npc_run_hp 3/3); team 12/12: out/lab/20261031-020249 (6 cold, lane 5)
  and out/lab/20261031-020425 (1 cold + 5 warm, lane 4).

## History
- 2026-10-31: opened.
- 2026-10-31: found and fixed on fix/w18-creature (regression of BUG-0281).
- 2026-11-01: released in 0.7.3.2.
