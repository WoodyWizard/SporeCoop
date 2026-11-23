---
id: BUG-1348
title: 0.7.16 final check: creature_kills reads the guest's NPC mirrors once, before they arrive after a warm reset
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: guest
versions: 0.7.16 (unreleased)
fixed_in: unreleased
branch: fix/w60-dna
commits: 48dfc9fa
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
Was waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_kills --lane 2 --lane 3 --retry 0 --no-build` PASS (best two or three
warm runs). no-fail-run: test change only (the FAIL is rel/0.7.16 worktree out/lab/20261120-041304).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): creature_kills PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-071312. Its FAIL in 20261123-023643 was the hunt (the kill on the guest did not count: "the guest's mirror has 0 health (6.0)"), not the mirror read: that run printed "21 mirrors on the guest after 0.6 s" and its mirror check passed. Closed.

## History
- 2026-11-20: opened.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
