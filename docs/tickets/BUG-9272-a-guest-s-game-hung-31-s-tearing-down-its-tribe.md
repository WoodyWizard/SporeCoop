---
id: BUG-9272
title: A guest's game hung 31 s tearing down its Tribe world for the host's world at a rejoin (watchdog in DestroyInstance)
status: fixed
kind: freeze
stage: Tribe
reported: 2026-11-20
source: found in the port of BUG-9268 (lane D, 2026-11-20)
role: guest
versions: 0.7.15.5 (feat/lineages-tribe)
fixed_in: unreleased
branch: feat/lineages-tribe
commits: 6d94b4ae 5e2418fa 1716e16c fafe5a96
tests: tools/tests/coop_tribe_seats.py (the lab hitch gate on its rejoin)
signature: WATCHDOG / hitch 12-31 s "Tribe, loading" on a guest; main thread in gamefixes forget_sender / forget_last_attacker (for_each_object, skg::readable) under DestroyInstance from GalaxyMenu_SimReset
docs:
related: BUG-9268
---

## Report
A guest with its own village (BUG-9268) left and joined again; leaving its Tribe world for the host's world its game
stood 31 s (sporekit WATCHDOG, hang report report-20261120-134212-i8.txt) and the host timed it out. In five more
rejoins (lane D, 2026-11-20): stalls of 1.6, 17.4, 12.1, 17.5 s and one past the 30 s watchdog.

## Root cause
Observed: the second watchdog's stack (coop.dll resolved with llvm-addr2line on the deployed DLL):
gamefixes::forget_sender -> game::for_each_object -> skg::readable (VirtualQuery) under the destroy hook
(gamefixes::destroy_guard) under cGameNounManager::DestroyInstance, from cCreatureCitizen_Teardown under
GalaxyMenu_SimReset. For every creature or citizen destroyed, forget_sender (BUG-0456) and forget_last_attacker
(BUG-0930) walk every object of the world with VirtualQuery per creature: a world reset destroys about 285-314 creatures
on this planet (the players' villages, the rivals, the animals), each walk over hundreds more. The suspected cursor list
was not the cause (the stalls came with `tribe.cursor_listed` on).

## Fix
GalaxyMenu_SimReset hooked (a flag around the call); while it runs the two walks are skipped (no NPC outlives a reset to
keep a stale stimulus or last attacker). Switch `coop.fix game.reset_sender_scan off`: the walks as before. The reset's
time is logged ("the world reset took N s"). The cursor guard stays behind `tribe.cursor_listed`.

## Verification
no-fail-run: not run in the Linux lab (no lane for this batch). Twin on Windows lane D, 2026-11-20, the guest's own
reset timed by the hook: `game.reset_sender_scan off`: 13.23 s and 16.47 s (two rejoins); on: 2.20, 1.53 and 1.51 s
(312, 283, 284 creatures destroyed without the walks). Five rejoins with the fix: no watchdog (one 11 s gap while
receiving the world, the BUG-0687 message-loop class, not the reset). The lab's hitch gate fails a stall on
coop_tribe_seats.py's rejoin.

## History
- 2026-11-20: opened.
- 2026-11-20: the cause found and fixed on feat/lineages-tribe (43c9dbbd).
