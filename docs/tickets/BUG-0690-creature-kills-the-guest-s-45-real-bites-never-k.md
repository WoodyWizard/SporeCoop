---
id: BUG-0690
title: creature_kills: the guest's 45 real bites never kill host NPC #20 (mirror at 2-6 hp, no kill on the host); fails on rel/0.7.11 base too
status: fixed
kind: test
stage: Creature
reported: 2026-11-07
source: fix/w46-p4-creature regression run 20261107-212113
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-g4-tests
commits:
tests: creature_kills
signature:
docs: docs/06-testing.md#testing-g4-tests
related: BUG-0618, BUG-0345
---

## Report
creature_kills FAIL on lanes 4 and 5: "the guest killed host NPC #20 (065CB8AB) with real input (45 bites)",
the guest's mirror at 2.0 / 6.0 hp, not dead, the host's kill count 0 -> 0. Seen on fix/w46-p4-creature
(out/lab/20261107-212113, 20261107-215918) and on the rel/0.7.11 base 2577101f (out/lab/20261107-220638): not caused
by this branch. Last pass 2026-11-01 (feat/w33-tests-creature). The same window's play_creature runs show the guest's
bot landing few hits (14 of 47 attacks, BUG-0618 class). Not analysed: whether the guest's bites miss (reach, the
target's flight), or land and are not applied by the host. Reproduce: `tools/lab.py run creature_kills`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (the test's new per-second trail of the host's NPC: hp@distance from the host, lab runs of 2026-11-08):
NPC #20 (species 065CB8AB, 6 hp) runs off as soon as the guest comes for it, before the first bite, and stops after
20-160 m (out/lab/20261108-052157: 69 / 78 m; 20261108-053723: 70 / 86 / 131 m; 20261108-053205 and -050538 / -051223:
200-210 m); the guest follows it all the way and kills it in 6-12 bites where it stops. The old hunt was 45 key presses
(~55 s, a click before every key): a long flight used them up, the mirror stood at 2-6 hp (FAIL 20261108-050538 with the
world's species, 20261108-051223 #1 with the guest's own creature). An experiment (out/g4/exp/flee.py, flee2.py): the
NPC does not walk off by itself (0 m in 10 s with nobody near) and steps 5-8 m off when the guest appears 2.5 m from
it; the long flight comes with the attack. Not caused by the mod as far as seen (the host's game runs the NPC's AI, the
flight ends and the kill counts on both games); not a lost bite (every bite at the stop lands: 6 hp in 6-7 keys).
Inferred: since the test last passed (2026-11-01) the chosen NPC (#20, the nearest of another species) is one that
flees far; which change made #20 that animal was not looked into.

## Fix
Test only (tools/tests/coop_creature_kills.py): the hunt is a player's (BUG-0618's recipe, docs/re/creature.md#creature-attack-reach):
the combat stance, one click on the animal, then the combat key (out of reach: the game's walk-and-use, in reach: a bite
when the ability is ready), another click only when the game lost the target; it lasts until the host's NPC is dead, at
most 90 s, instead of 45 keys. The checks are unchanged. The test prints the trail. KILLS_OLD_HUNT=1: the old loop (the
twin, fails on a long flight); KILLS_OWN=1: the guest hunts with its own creature (since BUG-0345 the world's species is
the default).

## Verification
FAIL (the old loop): out/lab/20261108-050538 (world species, mirror 4.0 hp after 45 bites, flight 210 m),
20261108-051223 #1 (own creature, 2.0 hp, flight 210 m); also the reports' 20261107-212113 / -215918 / -220638. The old
loop passes when the flight is short: 20261108-051223 #2 (106 m), 20261108-053723 #1-#3 (KILLS_OLD_HUNT=1, 70-131 m).
PASS (the new hunt) 9 of 9 on lane 2: out/lab/20261108-052157 #1-#3 (own creature), 20261108-052630 #1-#3,
20261108-053205 #1-#3 (world species; #2 / #3 caught after 200 m flights in 59 / 64 s).

## History
- 2026-11-07: opened.
- 2026-11-08 (fix/w46-g4-tests): the prey runs 20-210 m before it stops; the hunt is a player's and lasts until the kill; 9 of 9 PASS; fixed.
