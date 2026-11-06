---
id: BUG-0437
title: Tests: team fails warm after puppet_fight: the host's own family bites the host
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: fix/w34-ability-pop regression (lab 20261101-171437)
role: host
versions: 0.7.3.3+
fixed_in: 0.7.8
branch: fix/w44-tests
commits: 4f921102 f2037dd1
tests: tools/tests/coop_team.py, tools/tests/coop_puppet_fight.py
signature:
docs: docs/06-testing.md#testing-harness-g2
related: BUG-0414, BUG-0601
switch: none
---

## Report
Lab 20261101-171437 (lane 4, fix/w34-ability-pop, which changes no game behaviour): `team` ran warm right after
`puppet_fight` and failed in 46 s: the host's own family hit the host every 1.6-2 s (`damage: 0 hit by family
owner=npc posse=1 amount=1.0`, the monitor's posse_damage gate, 10 times), "a lone hit takes its damage from the
NPC in the host's game (0.0 of 3.0)", "joint hunt bonus ... None". `team` alone on fresh games passed (lab
20261101-173305).

## Root cause
Kind: **test setup** (the test's own choice of the NPC), not the game or the mod.
* Observed (lab 20261101-171437, team.log and team.host.game.log): team's hunt target was `#40 species=065CBE70`,
  2.1 m from the host; the host's own species is 065CBE70 (`creations: player 0 creature = 065CBE70`, the trace's
  `species=065CBE70`). So the joint hunt (`coop.npc hosthit 40`) made the host hit a member of its own family; the
  family bit the host back every 1.6-2 s (`damage: 0 hit by family owner=npc posse=1 amount=1.0`, the monitor's
  posse_damage gate); the family's hits on the guest's puppet were dropped (`dropped (npc.party_peace)` in the same
  log). The guest's lone hit on it took 0 hp: not analysed (inferred: the BUG-0374 peace rules between a guest and the
  host's party).
* Observed: team took the first NPC of `coop.npc list` that the guest mirrors with hp > 5, with no species filter. In
  cold games that is a wild animal ~50 m off (target #19-#21, species 065CB8AB in every cold log kept), in warm games
  the mod's numbering goes on (#40-#63) and after puppet_fight #40 was the host's family. puppet_fight leaves no
  state that the warm reset keeps (the reload restores the world); it only changes which NPC is listed first.
* Disproved: the inference of the report (a combat stance or a hostile family member kept from puppet_fight).

## Fix
tools/tests/coop_team.py: the hunt target is a mirrored NPC with hp > 5 that is not of the host's species and not
marked `family` in the host's list (the same filter as coop_creature_air.py). No check changed. No mod change, so no
switch.

## Verification
* FAIL of the old test: out/lab/20261101-171437 (fix/w34-ability-pop worktree): puppet_fight then team warm, target
  #40 of the host's species.
* PASS: out/lab/20261105-131546 (puppet_fight then team warm on lane 9, target #42 species 065CB811, no posse_damage
  line). See also the final regression runs in the G2 report.

## History
- 2026-11-01: opened (fix/w34-ability-pop).
- 2026-11-05 (fix/w44-tests, G2): root cause observed (the target was the host's own family), test fixed.
- 2026-11-05: released in 0.7.8.
