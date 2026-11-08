---
id: BUG-0328
title: Tribe: a guest that rejoins never gets the host NPCs it was sent while it still played its old copy (coverage 75%)
status: fixed
kind: desync
stage: Tribe
reported: 2026-10-30
source: lab 20261030-175722 campaign_tribe#1 (lane 7)
role: guest
versions: main de87f05
fixed_in: 0.7.4
branch: fix/w40-scale (the fix, BUG-0487); fix/w46-n2-desyncs (the Tribe twin)
commits: 59c64045 3fa7d38c d4f3ff94 cfa77d8b f07fcbb0
tests: npc_resync_tribe (coop_npc_resync.py --fixture tribe, the twin), tribe_rejoin_npcs (coop_tribe_rejoin_npcs.py, a scenario)
signature:
docs:
related: BUG-0288, BUG-0291, BUG-0125, BUG-0487
switch: npc.resync
---

## Report
Final verification lab 20261030-175722 campaign_tribe#1 (main de87f05, lane 7, run dir
out/lane7/campaign/20261030-193154): Dave left and joined again in tribe/play (19:39:02, back after
6 s, "left its old world: True"). Every sync check after that (19:39:48, 19:39:50, 19:39:56: the
play ended at 19:39:47) had "Dave: coverage 75% of 24, missing n:6 n63 n95 n96 n97 n98 n99"; the
campaign's desync needs two checks in a row, so it failed. Replay:
`SPORE_LANE=N python3 tools/tests/coop_campaign.py --from creature --to tribe --minutes 2.0 --seed 612535391`.

## Root cause
Not found. Observed (host game log): Dave joined at 20:39:05.3 (game clock), "appeared in the
world" 0.06 s later (its old copy), "hidden: flags=0x0 age=0.8 same_place=1" at 09.7 (loading the
host's world, its last state still in this place), appeared again at 13.9; the mod's own sync-check
at 21.4 was OK with Dave (22 objects). Dave's log: no proxy spawn failure, nothing about the 6.
Suspected: the host counted Dave present while it loaded (in Tribe `host_tick` does not check
PF_IN_WORLD / the state's age, the Creature path does through player_pos), so NPCs it noted as known
then were not sent again. A candidate fix (npc.rts_inworld: forget a Tribe guest's known NPCs while
it is not in the world) was written and left out: nothing showed it was needed.
Not reproduced: the replay with the candidate fix off 3 times (lane 7, 2026-10-30: Dave rejoined in
Tribe twice, once in Creature only; RESULT OK each time), and `coop_tribe_rejoin_npcs.py` (2 rounds,
fix off): coverage 100% (lab 20261030-195516-fix-w16-tribe).

2026-11-03 (fix/w41-tribe-world): the class is what npc.resync (BUG-0487, 0.7.4) handles: a guest that gets states
of an NPC it has no proxy of asks the host for its spawn again; the guest's receive path (npc.cpp, MSG_NPC_STATE)
does that in every stage, Tribe included (npc_stage covers mode 0x1654C02). Not verified in Tribe: a Tribe variant
of coop_npc_resync.py found no proxy rows in the census on the tribe fixture (`coop.census all 1200`: 76 / 75 rows,
all of kind `g`, while `coop.npc` listed 29 proxies; out/lab/20261103-235641), so it had nothing to forget (census.cpp lists `c` rows with net ids in Creature only: Tribe animals are plain `g` rows); the
variant was dropped. Next: pick the ids from `coop.npc` instead of the census.
2026-11-04 (fix/w43-realign): realign_tribe_rejoin (coop_realign_planet.py --fixture tribe --case rejoin) compares the whole planet after a guest's leave and rejoin in Tribe: villages, huts, food mats, nests the same (lab 20261104-212408, 20261104-212748); tools of far villages differed (8 missing) until wobj.tools_planet (BUG-0488). The NPC coverage of this ticket is not what that test measures.

2026-11-08 (fix/w46-n2-desyncs, N2 of 0.7.11): the state the report shows, shown in Tribe. What the 2026-10-30 guest
had is a guest without the proxies of host NPCs the host counts as known for it (the host sends a known NPC's spawn
once, then only its states; the rejoin's world load on the guest took the proxies made while it still counted as
in place). Observed on the tribe fixture: the guest destroys three of its proxies without telling the host
(`coop.npc forget <id>`, what that load leaves); with `SPORE_FIX_OFF=npc.resync` (the 0.7.3 code path: states of
unknown ids dropped) the three stay missing in the guest's game for good (coverage of `coop.sync`, the game's own
objects); with npc.resync (BUG-0487, released in 0.7.4) the guest asks for their spawns and its game has them again at
the host's places within 10 s. Inferred, not shown: that the 2026-10-30 rejoin lost them this way (the rejoin itself
did not lose NPCs again in 13 rejoins, the last two on this build: tribe_rejoin_npcs out/lab/20261108-030127).

## Fix
npc.resync (BUG-0487, 0.7.4, commits above): a guest that gets states of an NPC it has no proxy of asks the host for
its spawn again; the Tribe receive path is the same (npc_stage covers Tribe). Before: the scenario test `coop_tribe_rejoin_npcs.py` (suite `tribe_rejoin_npcs`) stays: a guest
rejoins twice in Tribe and must have the host's NPCs again (it passes on main).

## Verification
* FAIL with `SPORE_FIX_OFF=npc.resync`: out/lab/20261108-032117 npc_resync_tribe (lane 9: back [], missing [3, 6, 27]).
* PASS with the fix: out/lab/20261108-032237 npc_resync_tribe #1 #2 (lane 9: all 3 forgotten NPCs back each time,
  `asked=3` / `asked=6`).
* The rejoin scenario: tribe_rejoin_npcs OK out/lab/20261108-030127 (2 rounds, coverage 100 %).
Not reproduced; see the root cause. Logs of the replays: out/w16/replay_off*.txt of the fix/w16-tribe
worktree (not in git).

## History
- 2026-10-30: opened.
- 2026-11-03: looked at on fix/w41-tribe-world: covered by npc.resync in principle, not verified in Tribe.
- 2026-11-04 (fix/w43-realign): the rejoin's whole-planet objects checked (realign_tribe_rejoin PASS out/lab/20261104-212748); the NPC coverage still not reproduced, stays open.
- 2026-11-05 (fix/w45-tribe, T2 of 0.7.9): not reproduced on the fixture, also without npc.resync. Read in npc.cpp
  host_tick: in Tribe (rts) a guest that is not in this place (C.in_place false: loading, another world) gets every
  known NPC despawned and its known set cleared, so the NPCs are sent again when it is back; the window the ticket
  suspects is only while the host still counts it in place (its last state). tribe_rejoin_npcs (2 rounds of leave and
  join): coverage 100 % of 7 after each rejoin with `SPORE_FIX_OFF=npc.resync` (out/lab/20261105-233218) and with it
  (out/lab/20261105-233526): the test does not catch the class either way, so npc.resync (BUG-0487) is not shown to be
  its fix. A 4-player Cell -> Tribe campaign (campaign_ct, the original report's kind of run) is the remaining check:
  see below.
- 2026-11-06 (fix/w45-tribe): the remaining check done: four 4-player campaigns Cell -> Creature -> Tribe (campaign_ct,
  each with a guest's leave and rejoin during the Tribe play: Bob, Carol, Bob, Carol) had no coverage finding after
  the rejoin and ended RESULT OK (out/lab/20261105-233903, -235245, 20261106-000530, -001813). With the fixture runs
  above (2 x 2 rounds) and the earlier replays: the loss of the one 2026-10-30 run (main de87f05, before npc.resync of
  0.7.4, which re-asks for NPCs a guest gets states of and has no proxy of) was not seen again in 9 rejoins.
  Status not-reproduced; tribe_rejoin_npcs stays as the regression scenario.
- 2026-11-08 (fix/w46-n2-desyncs, N2): the class shown in Tribe with a FAIL/PASS twin (npc_resync_tribe, SPORE_FIX_OFF=npc.resync fails); closed as fixed by npc.resync of 0.7.4 (BUG-0487); the rejoin itself still did not lose NPCs (tribe_rejoin_npcs OK).
