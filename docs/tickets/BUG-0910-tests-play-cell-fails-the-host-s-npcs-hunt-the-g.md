---
id: BUG-0910
title: tests: play_cell fails 'the host's NPCs hunt the guest too' on a ratio of chase samples over the bots' random paths
status: fixed
kind: test
stage: Cell
reported: 2026-11-10
source: rel/0.7.12 lab out/lab/20261110-200504 (lane 9)
role: 
versions: 
fixed_in: unreleased
branch: fix/w47-play-bots
commits: 8b46435b, 6aef369f, 3b53e9b2, ab2e9a2f
tests: play_cell
signature:
docs: docs/re/cell.md#cell-hunt-trial, docs/06-testing.md#testing-w47-play-bots
related: BUG-0618
---

## Report
play_cell failed "the host's NPCs hunt the guest too (chased: guest 308, host 1180; in reach as prey: guest 48, host
94)" in the rel/0.7.12 lab run out/lab/20261110-200504 (lane 9); the same check failed before in rel/0.7.5
(20261104-061516), rel/0.7.7 (20261105-002102), fix/w46-detector-scale (20261107-141059 #1) and feat/w46-settings
(20261108-144402): 5 of the 45 play_cell runs whose logs are still on disk.

## Root cause
The test, not the mod. Observed (the 45 logs): the check compared sums of chase samples over the two bots' random
paths (the guest at least a third of the host's, unless it was in a hunter's prey range a third as often). Over all
45 runs the host's NPCs hunt the two players alike (chase samples host 24916 / guest 24165, in reach as prey 3225 /
3420; chase samples per prey sample, median: host 7.1, guest 6.4), but single runs go from 25 / 416 to 1269 / 339: a
pack that follows one player for a minute makes hundreds of samples, and the player hunted more also has more
samples per prey sample (9-12 against 4-6). The mirrored check (the host less than a third) would have failed once.
The game's own answers were the same for both players in every run (`prey_rules`: 0 of ~10000 differ).
Observed (lab lane 10, docs/re/cell.md#cell-hunt-trial): a hunter takes a player put into its prey range, alone,
within 0.2-2.1 s, the host's avatar and a guest's puppet alike: who is hunted in free play is decided by who swims
among the hunters.

## Fix
Test only, no limit loosened: the ratio is replaced by a controlled trial after the play (play_cell.py
`hunt_trial`): each player in turn is put 0.6 of the prey range from a hunter of the host's game that hunts no
player, the other player outside the range (`coop.pool prey 80 hunters`, `coop.pool swimto c<key> tp <m>`, both new),
and that hunter's chase target in the host's game (+0x22C) must become the player's cell within 8 s (5 hunters at
most). The host's turn is the control: the check fails when a hunter takes the host and none takes the guest.
Without a hunter to try, the play decides: the guest was a hunter's chase target, or was hardly ever in reach as
prey. `SPORE_CELL_HUNT_RATIO=1` adds the old ratio check. The puppet's level follow (the bug the check was written
for, docs/re/cell.md 9) had no twin switch: `coop.fix cell.puppet_level off` is the level of the moment the puppet
was made; with it off the trial fails.

## Verification
FAIL (the old ratio check, the mod unchanged): rel/0.7.12 out/lab/20261110-200504 and the four earlier runs of the
Report (5 of 45). PASS (the trial): the check passed in 10 of 10 runs on fix/w47-play-bots, out/lab/20261110-214743
#1-#3, out/lab/20261110-222127 #1-#4, out/lab/20261110-231644 #1-#3 (the guest chased 0.2-0.4 s after the move by the
first to third hunter tried). The trial catches the bug it guards: FAIL with `SPORE_FIX_OFF=cell.puppet_level`,
out/lab/20261110-230511 (the guest 0 of 5 hunters, the host 1 of 1; `prey_rules` 2666 of 14384 answers differ), a run
in which the old ratio check would have passed (chase samples host 380, guest 161).
Two of the ten runs failed other checks of play_cell, found by repeating and fixed in the test: a warm run read the
games' counters of the runs before (20261110-214743 #3, "the guest reports only what it ate": 183 = 57 + 70 + 56) and
the end food check read a moving total once (20261110-222127 #1, 210 / 204). The 6 runs after those fixes pass
(20261110-222127 #2-#4, 20261110-231644 #1-#3).

## History
- 2026-11-10: opened.
- 2026-11-10 (fix/w47-play-bots): the ratio replaced by a controlled trial; twin switch `cell.puppet_level`; warm
  counters and the end food check of play_cell; 10 of 10 runs pass the check, the fix-off run fails it.
