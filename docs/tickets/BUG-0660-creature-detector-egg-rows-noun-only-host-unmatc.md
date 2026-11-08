---
id: BUG-0660
title: Creature: detector egg rows (noun_only_host, unmatched, guest_only) in play_creature, 2-9 rows a run, not covered by the Tribe egg fix BUG-0474
status: fixed
kind: desync
stage: Creature
reported: 2026-11-06
source: lab log sweep follow-up 2026-11-06 (detector rows rel/0.7.8 vs rel/0.7.9)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: unreleased
branch: fix/w46-k8-open
commits: 61906153
tests: creature_family_eggs, creature_family_eggs_die, nest_eggs, nest_eggs_air, play_creature
signature:
docs: docs/re/creature.md#creature-family-eggs
related: BUG-0384, BUG-0474, BUG-0443
---

## Report
play_creature noun_only_host:egg rel/0.7.8 3-6 rows of n1, rel/0.7.9 2-9 rows of n1-3 (n3 in 20261106-153514); also unmatched/guest_only/host_only egg rows. BUG-0474 fixed the Tribe eggs only (nest.eggs_tribe). Related: BUG-0384.

## Root cause
Observed (rel/0.7.11 lab 20261107-223243 play_creature, SPORE_DETECTOR=20): the egg rows are at the players' own nest
((-170 -468 -84), the fixture's start) and stay for many samples (1 egg host-only from ~8 min, 2 later), not one
game's respawn (creature_family_eggs_die: the nest's eggs equal on both games after the host's death and hatch,
lab 20261108-004546). Reproduced (creature_family_eggs, fix off, lab 20261108-013018): the players' family herd (cHerd
+0x84 set) misses a member on the host (`coop.nest target`/`respawn`), a family member lays an egg (state 6) in the
host's game, the guest's game never has it (10 of 10 samples, 60 s). Cause (code): nest.eggs (BUG-0384) sends only
the eggs of NPC herds ("not the avatar's family eggs"); the family herd's members are NPCs mirrored on a guest (no AI,
no layer), so only the host's family lays.

## Fix
`nest.eggs_family` (nest.cpp): the host sends the family herd's eggs too (the state byte with 0x80: an older guest
skips a state over 16); a guest binds its own family egg within 4 m of a host family egg (the save's, both games have
it), puts it at the host's place (SetPosition + cEgg_SnapToGround, again when the host's moves or this game pushed it:
one stood 6.4 m off, 6.5 m over the ground) and follows its hatch / eat; a host family egg without one is mirrored
into the guest's own family herd (as nest.eggs does). `coop.nest` lists family_tx / family_made / own_bound /
own_placed. Off: each game's own family eggs (0.7.10).

## Verification
creature_family_eggs (`coop_creature_family_eggs.py`: the host's family herd lays an egg; for 60 s every egg within
70 m of the nest pairs one to one within 2 m between the games), lane 4:
- FAIL with SPORE_FIX_OFF=nest.eggs_family: out/lab/20261108-013018 (10 of 10 samples: the laid egg host-only); also
  20261108-005344 (an earlier version of the test).
- PASS: out/lab/20261108-011630 (x2, with creature_family_eggs_die x2, nest_eggs x2, nest_eggs_air x2: 8/8).
  (20261108-010438 #1 FAILED with the first version of the fix: the guest's own save egg drifted 6.4 m and stayed
  bound there; the own-egg placement fixed it.)
- Detector: see the History line of the final play_creature run.

## History
- 2026-11-06: opened from the lab log sweep follow-up; planned for 0.7.10.
- 2026-11-08: fixed on fix/w46-k8-open (nest.eggs_family).
- 2026-11-08 (detector, play_creature SPORE_DETECTOR=20, lane 4): base rel/0.7.11 ef329e5c (a temporary worktree, run 20261108-022007 there, removed since) 16 egg rows in 27 samples (host_only 5, noun_only_host 9, guest_only 1, noun_only_guest 1); fix/w46-k8-open out/lab/20261108-023113 2 in 28 (`noun_only_host:egg` whole-list, no near row: an NPC nest's egg beyond nest.eggs' 110 m relevance, by design). That play_creature FAILED on `joint hunts happened (+0 joint hits)`, a bot outcome seen failing 7 times in other branches' runs (not K8).
- 2026-11-08 (final run after merging rel/0.7.11 ccfb0ae4, out/lab/20261108-030826): play_creature PASS; egg rows 6 in 28 samples (base 16 in 27): `noun_only_host` whole-list 4 (eggs beyond the 110 m relevance), 1 `guest_only` + 1 `noun_only_guest` at the nest in one sample (the guest's own egg, transient). detect_creature 0 rows.
