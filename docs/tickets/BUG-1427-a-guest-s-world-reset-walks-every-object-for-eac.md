---
id: BUG-1427
title: A guest's world reset walks every object for each creature it destroys: seconds of stall leaving a world (BUG-9272 analogue)
status: open
kind: freeze
stage: All
reported: 2026-11-22
source: pr
role: guest
versions: 0.7.16.1
fixed_in:
branch: fix/w63-net
commits:
tests: reset_stall, reset_stall_off
signature:
docs: docs/re/tribe.md#tribe-reset-walks
related: BUG-0456, BUG-0930, BUG-0791
---

## Report
The analogue of BUG-9272 that IltonBJSilva found and fixed in their fork (PR #212, commit 6d94b4ae6 "no object walks of
the destroy hook during the world reset"; credit to them, their fix read as data and done again in our code): a
guest's game stood 12-31 s (once past the 30 s watchdog, and the host timed it out) while it tore down its Tribe world
for the host's world at a rejoin; the main thread in forget_sender under DestroyInstance under GalaxyMenu_SimReset.
Our destroy hook (gamefixes.cpp destroy_guard at 5376d388) has the same two walks for every creature or citizen
destroyed, with no exception for a world reset.

## Root cause
From the code (the cost by reasoning, docs/re/tribe.md#tribe-reset-walks): forget_sender (BUG-0456) walks the whole
noun list for each destroyed animal or citizen and calls skg::readable (VirtualQuery) twice for each live one;
forget_last_attacker (BUG-0930) walks again with one more readable each. A reset destroying D of A creatures costs
~2 * D walks of the list and ~1.5 * D * A VirtualQuery: O(n^2), 135 000 system calls for the fork's 300 creatures on
Windows (VirtualQuery is a system call there, BUG-0791). Their measurement: 13.2 / 16.5 s with the walks, 0.9-2.2 s
without. Under Wine a VirtualQuery is cheap, which is why our lab never showed it.

## Fix
gamefixes.cpp hooks `SporeCoop::GalaxyMenu_SimReset` (0xB32C30, verified: cdecl void(), one exit; the symbol was
likely) and skips both walks while it runs: the reset leaves no NPC whose stimuli or last attacker could name the dying
creature (they all go, no AI runs inside the call, their references are counted). The avatar's target check (O(1))
stays. The reset's time (performance counter), its creatures, the destroys walked / not walked, the time in the walks
and the readable calls are logged and in `coop.status` ("world resets N ... the last: secs= ..."). Switch
`coop.fix game.reset_no_walks off`: a walk for every destroy (0.7.16).

## Verification
Open, waiting for the lab run. `tools/lab.py run reset_stall reset_stall_off`: the guest's readable() is slowed to
~50 us a call (`coop.perf readable extra n`, as a slow Windows PC) and it leaves its Tribe world for the galaxy menu;
reset_stall must PASS (at least 30 creatures destroyed in the reset, the walks under 0.5 s, the reset under 10 s),
reset_stall_off must FAIL (the walks take seconds). If reset_stall finds fewer than 30 creatures in the reset, the
teardown runs outside GalaxyMenu_SimReset in our flow: stop and record it (the fix would not apply).

## History
- 2026-11-22: opened.

- 2026-11-22: renumbered from BUG-9312 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9312.
