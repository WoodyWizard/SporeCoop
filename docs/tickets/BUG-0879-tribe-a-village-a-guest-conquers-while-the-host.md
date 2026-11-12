---
id: BUG-0879
title: Tribe: a village a guest conquers while the host is away is not conquered on the host, and the host's return undoes it
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w48-captures
commits: be1ce456, 5a073eb7, 3ec69627
tests: tribe_away_conquest
signature:
docs: docs/re/tribe.md#tribe-conquest-sync
related: BUG-0777, BUG-0531
---

## Report
The reverse direction of BUG-0777 (Discord 2026-11-09). The host is in the outfit editor / a menu / loading; the guest finishes off an NPC village: its own game plays the conquest. When the host is back the village stands again and the guest's goal progress is gone.

## Root cause
* Observed (lab 20261112-004847, tribe.away_conquest off): with the host in the main menu the guest's raid ended the
  fixture's NPC village in the guest's game (+0x556 set); after the host's reload the host's village stood (hut
  1000/1000, not conquered) while the guest's stayed conquered.
* Cause (code): tribe.away lets the guest's game play; nothing was sent to the host.

Earlier reading (2026-11-10):
Inferred from the code, confidence medium-high: `mods/coop/tribe.cpp:67` `local_play()` (host in a menu, an editor over 4 s, a load, another world; set in `coop.cpp:6713`): realign is off, so the guest's game destroys the hut and plays the conquest by itself; nothing is sent to the host. On the host's return `s_goal` (`wsync.cpp:407`) overwrites the guest's goal progress, realign repairs the hut and `make_npc_tribes` (`tribe.cpp:2212`) can make the village again.

## Fix idea
In local play keep an NPC hut at 1 hp and refuse to finish a conquest (with a notice), or record the outcome and send it to the host when it is back (the host then conquers the village in its game). Belongs with the outcome row of BUG-0777.

## How to test
`coop_tribe_away.py` plus a guest raid to hut health 0 while the host is away; after the host's return compare `coop.realign planet hut` / `village` and `coop.wsync dump tribe_goal` on both games: the same outcome on both.

## Fix
tribe.cpp: the guest watches its NPC villages' +0x556; one set while the host is away goes to the host as
MSG_TRIBE_CMD kind 4 (CMD_CONQUER, position), held until the host is back in Tribe (BUG-0693); the host fells that
village's hut with a blow by a member of that player (the game's own conquest follows). `coop.tribe conquests`.
Switch `coop.fix tribe.away_conquest off` (0.7.12).

## Verification
`coop_tribe_away_conquest.py` (suite `tribe_away_conquest`, tribe): host `coop.menu`, the guest raids the fixture's
NPC village in its own game (hut cut to 2 hp), the host loads the same save; within 60 s the host's village is
conquered and its hut down, the guest's too.
* FAIL with `SPORE_FIX_OFF=tribe.away_conquest`: lab 20261112-004847.
* PASS: lab 20261112-004613, 20261112-005111 (lane 5).

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
- 2026-11-12 (fix/w48-captures): fixed, FAIL and PASS runs in Verification.
