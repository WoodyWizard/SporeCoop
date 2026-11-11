---
id: BUG-0879
title: Tribe: a village a guest conquers while the host is away is not conquered on the host, and the host's return undoes it
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0777, BUG-0531
---

## Report
The reverse direction of BUG-0777 (Discord 2026-11-09). The host is in the outfit editor / a menu / loading; the guest finishes off an NPC village: its own game plays the conquest. When the host is back the village stands again and the guest's goal progress is gone.

## Root cause
Inferred from the code, confidence medium-high: `mods/coop/tribe.cpp:67` `local_play()` (host in a menu, an editor over 4 s, a load, another world; set in `coop.cpp:6713`): realign is off, so the guest's game destroys the hut and plays the conquest by itself; nothing is sent to the host. On the host's return `s_goal` (`wsync.cpp:407`) overwrites the guest's goal progress, realign repairs the hut and `make_npc_tribes` (`tribe.cpp:2212`) can make the village again.

## Fix idea
In local play keep an NPC hut at 1 hp and refuse to finish a conquest (with a notice), or record the outcome and send it to the host when it is back (the host then conquers the village in its game). Belongs with the outcome row of BUG-0777.

## How to test
`coop_tribe_away.py` plus a guest raid to hut health 0 while the host is away; after the host's return compare `coop.realign planet hut` / `village` and `coop.wsync dump tribe_goal` on both games: the same outcome on both.

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
