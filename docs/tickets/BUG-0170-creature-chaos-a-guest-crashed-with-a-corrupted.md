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
branch: fix/w1-creature
commits:
tests: chaos_creature_long
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
Unknown. Not reproduced on 2026-10-27 (see Verification). Inferred only: heap corruption from an
earlier write to a freed object. Two such paths on the guest were found and fixed in the same wave
(BUG-0091: animations played on puppets freed by a world teardown, 15 in 6 forced rounds; BUG-0082:
kill-list pointers kept across the host's absence). Neither is shown to be this crash.

## Fix
No fix of its own. BUG-0091 and BUG-0082 (fix/w1-creature) remove two candidates.

## Verification
What was run (fix/w1-creature, lane 4, GOG 2024 dc04aee5; lab run out/lab/20261027-183439):

* `coop_chaos.py --fixture creature --minutes 6` seed 417787344 on main 88341cf: 904 actions in 362 s
  (4 rejoins, 7 saves, 76 stage actions, 275 targets, 16 outages). OK, 0 faults on both games.
* The same, seed 71370752, with the BUG-0082/0091 fixes deployed: 839 actions, 8 rejoins. OK, 0 faults.
* Not run: the 4-player replay `coop_multi.py --players 4 --seed 1035603402`. It needs two lanes and
  this wave had one lane per agent. The freed-pointer census (branch dev/census) is unfinished WIP
  ("not tested") and is not on main, so it was not used.

Next step when it shows again: replay seed 1035603402 with 4 players on two free lanes, with
`coop.framefix 0` and `1`.

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-27: not reproduced on fix/w1-creature (2 creature chaos runs, 0 faults); two freed-pointer candidates fixed (BUG-0082, BUG-0091).
- 2026-10-30: fix/w13-creature: chaos_creature_long (6 min, 2 players) OK in out/lab/20261030-141545 (fix/w13-creature, lane 5, GOG 2024), 0 faults on both games. The 4-player replay (seed 1035603402) was not run (lanes 4 and 5 were busy with this batch's tests). Not reproduced; stays.
