---
id: BUG-1329
title: Civ: a guest's own arrival editor's result faults at 0x645E4D in the Tribe goal listener
status: open
kind: crash
stage: Civ
reported: 2026-11-19
source: lab
role: guest
versions: 
fixed_in:
branch:
commits:
tests: civ_late_arrival, creation_props_key0
signature: 0x00645E4D hash_map__int_int__Find+0xD < cIdentityColorable::AssignNames+0x1443 < TribeStage_RemoveMemberCard+0x1D8F < TribeGame_GoalListener+0x163
docs:
related: BUG-1276, BUG-1290, BUG-1328
---

## Report
Lab (found on the way by fix/w58-cityname, not fixed there): when the guest's own City Hall editor at its Civ arrival
(WP-X1, core.own_civ_arrival / core.own_civ_late) ends, the editor's result message 0x30C11C7 (tag 0x116DD1B, the end
of Tribe's) reaches SporeCoop::TribeGame_GoalListener 0xCD81F0, which is still listening in Civ, and the game takes a
first-chance access violation (read of 0x30) that the game survives under Wine:

    exception 0xC0000005 at 0x00645E4D hash_map__int_int__Find+0xD (data 0x00000030), ebp=030C11C7
    called from: Simulator::cIdentityColorable::AssignNames+0x1443 < SporeCoop::TribeStage_RemoveMemberCard+0x1D8F
    < SporeCoop::TribeGame_GoalListener+0x163 < App::cMessageManager::MessageSend

Seen: fix/w57-own-editors 20261119-113002 civ_late_arrival (a saved City Hall, lane 2), 20261119-130049
civ_late_arrival_unpause_off, fix/w58-cityname 20261119-200001 creation_props_key0_off and 20261119-200557
creation_props_key0 (both: the City Hall editor left with the red cross, lane 6). On civ_late_arrival's route (the guest's game restarted, joining the host's Civ world) every result of the guest's
own City Hall editor faulted (3 of 3 logs that hold one: 20261119-113002, 20261119-130049, and fix/w58-cityname
20261119-201729 creation_props_key0 with a saved City Hall, lane 6); civ_late_arrival itself passes because it restarts
the guest again afterwards: the lab keeps the last instance's log and fault count only. On civ_own_arrival's route (the
live end of Tribe) no run had it (20261119-200005 and the fix/w57-own-editors runs). The test's fault check fails the test. On Windows a first-chance AV in game code may
end the game (as BUG-0304's did).

## Root cause
Not analysed. Inferred: the Tribe game's goal listener handles the end of Tribe's editor result (tag 0x116DD1B) and
reads the tribe's member cards / identity colours, gone in Civ; the arrival editor reuses that tag (civ_arrival.cpp
kTribeTag) so the stale listener runs on it.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened (fix/w58-cityname, found while testing BUG-1328; outside that task).
