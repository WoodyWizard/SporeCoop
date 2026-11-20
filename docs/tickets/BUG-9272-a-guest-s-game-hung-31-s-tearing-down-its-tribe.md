---
id: BUG-9272
title: A guest's game hung 31 s tearing down its Tribe world for the host's world at a rejoin (watchdog in DestroyInstance)
status: open
kind: freeze
stage: Tribe
reported: 2026-11-20
source: found in the port of BUG-9268 (lane D, 2026-11-20)
role: guest
versions: 0.7.15.5 (feat/lineages-tribe)
fixed_in:
branch: feat/lineages-tribe
commits:
tests:
signature: WATCHDOG no frames 30 s; main thread in coop.dll gamefixes::destroy_guard (skg::readable) under cGameNounManager::DestroyInstance from GalaxyMenu_SimReset
docs:
related: BUG-9268
---

## Report
The guest left the session in Tribe and joined again; the host shared its world; the guest's game left its Tribe world
for the galaxy menu to load it and stood 31 s (sporekit WATCHDOG, hang report report-20261120-134212-i8.txt, hitch
31251 ms "Tribe, loading"); the host timed the guest out. One sample: the main thread in
coop.dll gamefixes::destroy_guard -> skg::readable (VirtualQuery) under DestroyInstance from GalaxyMenu_SimReset. The
next rejoin of the same session and the later ones had no hang.

## Root cause
Not known. Suspected: the guest's terrain cursor candidate list (+0x164) bound to its own village (BUG-9268,
`tribe.guest_native`) while that village was being destroyed: the hook kept binding while the stale village pointer read
as alive (game::object_alive), so the cursor could walk a freed vector during the teardown. The watchdog's one sample
does not show it.

## Fix
Guarded in BUG-9268 before its commit: the cursor list is bound only while the village is in the object list
(game::listed, safe on a freed pointer) and no load is queued, and the game's own list is put back otherwise. Not shown
to be the cause: the ticket stays open until a rejoin test runs without the hang several times (coop_tribe_seats.py
check 5).

## Verification
Repro: lane D, a guest with its own village (BUG-9268) leaves and joins again while the host plays on in Tribe (a
cinematic of the first rival was starting on the host at the time).

## History
- 2026-11-20: opened.
