---
id: BUG-0328
title: Tribe: a guest that rejoins never gets the host NPCs it was sent while it still played its old copy (coverage 75%)
status: open
kind: desync
stage: Tribe
reported: 2026-10-30
source: lab 20261030-175722 campaign_tribe#1 (lane 7)
role: guest
versions: main de87f05
fixed_in:
branch:
commits:
tests: tribe_rejoin_npcs (coop_tribe_rejoin_npcs.py, a scenario)
signature:
docs:
related: BUG-0288, BUG-0291, BUG-0125, BUG-0487
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

## Fix
None. The scenario test `coop_tribe_rejoin_npcs.py` (suite `tribe_rejoin_npcs`) stays: a guest
rejoins twice in Tribe and must have the host's NPCs again (it passes on main).

## Verification
Not reproduced; see the root cause. Logs of the replays: out/w16/replay_off*.txt of the fix/w16-tribe
worktree (not in git).

## History
- 2026-10-30: opened.
- 2026-11-03: looked at on fix/w41-tribe-world: covered by npc.resync in principle, not verified in Tribe.
- 2026-11-04 (fix/w43-realign): the rejoin's whole-planet objects checked (realign_tribe_rejoin PASS out/lab/20261104-212748); the NPC coverage still not reproduced, stays open.
