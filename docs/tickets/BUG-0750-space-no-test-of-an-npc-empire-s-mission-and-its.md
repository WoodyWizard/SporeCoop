---
id: BUG-0750
title: Space: no test of an NPC empire's mission and its reward on the partner's game
status: fixed
kind: test
stage: Space
reported: 2026-11-08
source: BUG-0199 (fix/w46-g4b-coverage, 2026-11-08)
role: 
versions: 0.7.10 and earlier
fixed_in: unreleased
branch: fix/w46-b1-space
commits: 879f3063
tests: space_npc_mission
signature:
docs: docs/re/space.md#space-npc-mission-offer
related: BUG-0199
---

## Report
Child of BUG-0199. A mission of an NPC empire done by one player: its reward (money, cEmpire::AddMoney in the base
OnCompleted) must show in the partner's game through the shared money.

Attempt (fix/w46-g4b-coverage, lane 3, out/exp_mis.py on `space_empire`, 2026-11-08), the clicks in
docs/re/space.md#space-npc-store-runtime: the guest jumped to 0046102D, comm (288, 914), first answer (720, 725),
Missions (838, 754), "Any missions?" (720, 725): the empire offered a mission (a mission noun, state 0, id 2AB57FAC,
`coop.space allies missions` lists it; new test command `coop.space allies missions [done <ptr>]` = SetState(5)).
"Tell me more..." showed a Galactic Adventures mission ("Beam Down?"), which would start an adventure; declining it
removed the noun and the empire had no new offer for a while.

Recipe for the test: ask both contacted empires (80001FCC at 0046102D, 80001F53 at 00460038) until a non-adventure
mission is offered, accept it by clicks, complete it with `coop.space allies missions done <ptr>` (or play it),
assert the reward +0xA0 arrives in the taker's money and the partner's game shared money rises by it. Twin:
`SPORE_FIX_OFF=res.guest_send` for a guest's mission.

## Root cause
A test gap. The blocker was not randomness: every NPC empire offers the Galactic Adventures intro (2AB57FAC) first
until it is done (ChooseMission 0xFEEBC0; declining only puts the planet on cooldown). Observed on lane 6:
docs/re/space.md#space-npc-mission-offer. No co-op defect found: the reward reaches the partner through the shared
money.

## Fix
No mod change (a test gap). New test `coop_space_npc_mission.py` (suite `space_npc_mission`): the guest asks the
first empire, the intro is completed by `coop.space allies missions done` (test setup), the second empire offers a
regular mission (reward 30,000) which the guest accepts by clicks and completes; the host's shared money must rise
by the reward. Switch for the broken variant: `res.guest_send` (BUG-0199's twin).

## Verification
* FAIL with `SPORE_FIX_OFF=res.guest_send` (the broken variant: a guest's earning stays in its game): lab 20261108-160114 (the guest +30,000, the host unchanged).
* PASS: lab 20261108-160317 and 20261108-160505 (fix/w46-b1-space, lane 6). Direct run before (SPORE_LANE=6): OK, guest and host
  1,100,000 -> 1,130,000.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): opened as a child of BUG-0199 after an attempt with a run (recipe and blocker above).
- 2026-11-08 (fix/w46-b1-space): the intro-mission blocker understood; test written (no mod change).
