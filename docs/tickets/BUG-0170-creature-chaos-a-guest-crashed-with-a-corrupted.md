---
id: BUG-0170
title: Creature chaos: a guest crashed with a corrupted heap (seen once)
status: not-reproduced
kind: crash
stage: Creature
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w1-creature; tested on fix/w46-n1-crashes
commits:
tests: chaos_creature_long, multi4_creature_seed, multi4_creature_slow, multi4_players
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `coop_multi.py --fixture creature --players 4 --minutes 6`, seed 1035603402, lanes 1+8, 2026-10-23
  (branch fix/npc-lag): Bob (lane A guest, never rejoined) got a first-chance AV at 0x009ABB5C
  (`test byte [eax],1` after `call 0x9AC1B0`, eax 0xC8124380) 0.1 s after "Carol pings a place",
  then an AV in `GeneralAllocator_FreeInternal`+0xAD 3 s later, and froze in `MallocInternal` (watchdog:
  no frames). Looks like heap corruption from earlier; the bot's last actions on Bob were clicks,
  wheel and `coop.targets`. Not seen in 4 other runs of the same test. Replay with the seed above.

## Root cause
Unknown; not reproduced (0.7.11 N1, see Verification). Inferred only: heap corruption from an earlier write to a
freed object (a first-chance AV at 0x009ABB5C, then GeneralAllocator_FreeInternal, then a hang in MallocInternal).
Two such guest paths were fixed in the same wave (BUG-0091: animations played on puppets freed by a world teardown,
`core.frame`; BUG-0082: kill-list pointers kept across the host's absence, `npc.kill`). Reasoning for closing:
* The report's own replay (4 players, seed 1035603402, 6 min) was run for the first time, 6 times on the current
  build: 4 with all fixes on (also at 20 fps over the players' networks), one with each candidate's switch off: no
  fault, no freeze on any of the 4 games. With core.frame off no puppet call met an unlisted puppet (no "gone already"
  line in the 4 games' logs): the teardown order that freed puppets under the mod in 0.7.2.4 does not happen any more.
* A bot replay is not deterministic (timing, 576-604 actions per run against the report's unknown count), so this
  does not prove the exact action order was replayed; the report's build was 0.7.2.4 main, since then the freed-object
  class was worked through in several releases (BUG-0091, 0082, 0029, 0054, 0066 among them).

## Fix
No fix of its own. BUG-0091 and BUG-0082 (fix/w1-creature) remove two candidates.

## Verification
What was run (fix/w1-creature, lane 4, GOG 2024 dc04aee5; lab run out/lab/20261027-183439):

* `coop_chaos.py --fixture creature --minutes 6` seed 417787344 on main 88341cf: 904 actions in 362 s
  (4 rejoins, 7 saves, 76 stage actions, 275 targets, 16 outages). OK, 0 faults on both games.
* The same, seed 71370752, with the BUG-0082/0091 fixes deployed: 839 actions, 8 rejoins. OK, 0 faults.

0.7.11 N1 (fix/w46-n1-crashes, GOG 2024), all OK, 0 faults on every game, no freeze:
* multi4_creature_seed (`coop_multi.py --fixture creature --players 4 --minutes 6 --seed 1035603402`, 4 players in one
  lane): out/lab/20261107-223550 (lane 10, 604 actions, 5 rejoins), out/lab/20261107-233741 (lane 10),
  out/lab/20261107-235504 (lane 9, SPORE_FIX_OFF=core.frame), out/lab/20261108-000828 (lane 10, SPORE_FIX_OFF=npc.kill).
* multi4_creature_slow (the same seed at 20 fps over the players' networks): out/lab/20261107-223550 (582 actions),
  out/lab/20261107-235504 (SPORE_FIX_OFF=core.frame).
* multi4_players (4 players, 3 min, SPORE_NET=players) and chaos_creature_long (6 min, 874 actions, 4 rejoins):
  out/lab/20261107-233528 (lane 9).

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-27: not reproduced on fix/w1-creature (2 creature chaos runs, 0 faults); two freed-pointer candidates fixed (BUG-0082, BUG-0091).
- 2026-10-30: fix/w13-creature: chaos_creature_long (6 min, 2 players) OK in out/lab/20261030-141545 (fix/w13-creature, lane 5, GOG 2024), 0 faults on both games. The 4-player replay (seed 1035603402) was not run (lanes 4 and 5 were busy with this batch's tests). Not reproduced; stays.
- 2026-11-08 (fix/w46-n1-crashes, 0.7.11 N1): the report's 4-player seed replayed 6 times (also at 20 fps over the players' networks, and with core.frame / npc.kill off): no fault; stays not-reproduced, closed with the runs.
