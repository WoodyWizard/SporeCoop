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
fixed_in:
branch: fix/w47-play-bots
commits:
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
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened.
