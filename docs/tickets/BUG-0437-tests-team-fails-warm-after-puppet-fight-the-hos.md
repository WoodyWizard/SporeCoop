---
id: BUG-0437
title: Tests: team fails warm after puppet_fight: the host's own family bites the host
status: open
kind: test
stage: Creature
reported: 2026-11-01
source: fix/w34-ability-pop regression (lab 20261101-171437)
role: host
versions: 0.7.3.3+
fixed_in:
branch:
commits:
tests: tools/tests/coop_team.py, tools/tests/coop_puppet_fight.py
signature:
docs:
related: BUG-0414
---

## Report
Lab 20261101-171437 (lane 4, fix/w34-ability-pop, which changes no game behaviour): `team` ran warm right after
`puppet_fight` and failed in 46 s: the host's own family hit the host every 1.6-2 s (`damage: 0 hit by family
owner=npc posse=1 amount=1.0`, the monitor's posse_damage gate, 10 times), "a lone hit takes its damage from the
NPC in the host's game (0.0 of 3.0)", "joint hunt bonus ... None". `team` alone on fresh games passed (lab
20261101-173305).

## Root cause
Not analysed. Inferred: puppet_fight leaves the games in a state the next warm test does not expect (combat
stance, a family member hit by a click of the fight and now hostile, health changed); the fight clicks NPCs on
screen, which may include the host's own family.

## Fix
Not fixed. Ideas: puppet_fight sets the social stance back and its targets skip the fighter's own species, or
it is not warm (`warm=True` in suite.py) for the tests after it.

## Verification
no-fail-run: not fixed (found in a regression run of another branch).

## History
- 2026-11-01: opened (fix/w34-ability-pop).
