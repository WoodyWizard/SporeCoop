---
id: BUG-1348
title: 0.7.16 final check: creature_kills reads the guest's NPC mirrors once, before they arrive after a warm reset
status: open
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: guest
versions: 0.7.16 (unreleased)
fixed_in:
branch: fix/w60-dna
commits:
tests: creature_kills
signature:
docs:
related: BUG-0015, BUG-0016
---

## Report
rel/0.7.16 worktree out/lab/20261120-041304 creature_kills FAIL "host NPCs mirrored on the guest (0)" and then "an NPC far from the host (0)".

## Root cause
A test race: creature_kills.guest.game.log: the guest in Creature at 09:25:38.567 with "npcs mirrored: 0 within 60 m,
0 in all", 09:25:39.122 "8 within 60 m, 21 in all". The warm reset's settle (both games' time runs, 0.6 s) ended in
between and the test read the guest's `coop.npc list` once. The mirrors come with the host's first NPC states. Earlier
FAILs of this test had other causes (the hunt); the mod is right.

## Fix
coop_creature_kills.py waits for the state: the near mirrors up to 30 s (as coop_team.py), the far ones up to 10 s;
the wait is printed ("N mirrors on the guest after X s"). No check changed.

## Verification
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_kills --lane 2 --lane 3 --retry 0 --no-build` PASS (best two or three
warm runs). no-fail-run: test change only (the FAIL is rel/0.7.16 worktree out/lab/20261120-041304).

## History
- 2026-11-20: opened.
