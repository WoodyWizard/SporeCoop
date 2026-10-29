---
id: BUG-0136
title: a won raid on a partner's colony can count as aborted, so the owner's raid is never completed
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#115
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-space
commits: 66d7a30
tests: unit (tests/space_tests.cpp)
signature:
docs: docs/re/space.md#space-w3-trips-clicks-defenders
related: #115
switch: allies.aborted
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/115 (full text, discussion).

`space_allies.cpp` keeps the raid missions it aborted itself in `std::set<void*> g_aborted`, compared by address only. Entries leave the set only in `watch_raids`, and only for missions that were in `g_watch`. But `helper_raids` also aborts raids that are still accepted (state 2), and those never enter `g_watch`, which only takes active raids (state 3). Their pointers stay in `g_aborted` for good. `end_all` (session end) clears `g_watch` but not `g_aborted`, so they also survive the session.

When the game later creates a raid mission at a recycled address, and that raid is watched and won, `watch_raids` finds its address in `g_aborted` and treats the win as an abort. No K_DEFENDED goes to the colony's owner, so the owner's raid is never completed and the "colony is safe" notice is not shown.

### Where
- Declaration: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L101
- `active_raids(true)` includes state 2: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L636-L646
- Store, in `helper_raids` (aborts state 2 and state 3 raids): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L699-L718
- `g_watch` only takes state 3 raids (`active_raids()`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L760-L773
- Use, the only erase: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L774-L800
- Session end does not clear it: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L932-L945

### How to reproduce
1. Two lanes, `space` fixture, with allies mirroring on (a mirror colony of the partner in my galaxy).
2. The partner starts a raid on that colony (`raid -start` path, as the allies test does). I fly onto the mirror planet and leave again within about 2 s of arriving (before my helper raid turns active).
3. `coop.space allies` on my side: the aborted count grows, while `g_aborted` keeps the entry (add a size to the status line to see it).
4. Repeat until a later helper raid at the same mission address is won. That needs allocator reuse, so check the mission pointers in the log.

### Expected
Only raids that are aborted, and still exist, count as aborted, and the set is empty after a session.

## Root cause
**What happens**

1. I am on a partner's mirror colony while the partner's raid runs. `helper_raids` starts a raid of mine there (StartRaid). The new mission is in state 2 until the manager's next update, seconds later.
2. I leave the planet within those seconds, or the owner's raid ends. `helper_raids` sets my raid to 7 (aborted) and inserts it into `g_aborted`. It was never in `g_watch`, so it is never erased.
3. The mission object is freed. Later a new raid mission (mine on a mirror colony, in this session or a later one) is allocated at the same address, enters `g_watch`, and is won: all its ships are destroyed.
4. `watch_raids`: `g_aborted.erase(...)` returns 1, so the win is skipped. No K_DEFENDED, `raids_won` does not grow, and the owner's raid stays open on the owner's machine.

### Confidence
- The leak is CONFIRMED by reading.
- The wrong outcome is PLAUSIBLE: it needs a recycled mission address. Not run.
- A second reviewer verified it and rated it low.

## Fix
Fixed on fix/w3-space (66d7a30): after each `watch_raids` only aborted raids that are watched stay in `g_aborted` (`spacelogic::prune_aborted`, mods/coop/space_logic.h), and `end_all` clears it. `coop.space allies abortedfix 0` keeps the old set (it also prints the set's size).

Suggested in the review:

- In `watch_raids`, drop every `g_aborted` entry that is no longer in `missions()` (or no longer a raid in state 2 or 3).
- Clear `g_aborted` in `end_all`.
- Alternatively, store the aborted raid's PlanetID plus the abort time instead of the pointer.

## Verification
Done (2026-10-28) as a unit test (the outcome needs the allocator to reuse a mission address, which a two-game test cannot force): tests/space_tests.cpp `test_aborted`: an accepted raid aborted before it was watched, then a raid at the same address watched and won. Without the prune (the old code) the win counts as aborted (that branch is checked in the same test); with the prune it counts as won; an aborted raid that is watched stays until its end is handled. `make test` passes. Not run in the game.

Planned in the review: Planned test (must FAIL without the fix and PASS with it):

Two-instance allies test as in the steps, with a status counter for `g_aborted` size. After the aborted helper raid has disappeared from the mission list, assert the size is 0.

- FAILS on main: it stays 1.
- PASSES with the fix.

For the effect: force the next watched raid to reuse the pointer (debug: insert its address into `g_aborted`). Then assert that the owner's raid mission reaches state 5 on the owner's machine after the win.

### Related
#24 (armada contract raid handling), #16 and #49 (allies mirror lifetime).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #115.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-space (66d7a30): aborted set pruned to watched raids and cleared at the session end; unit test.
- 2026-10-28: fix/w3-space lab runs on lane 6: 20261028-010933 (14/14: the four new wave 3 Space tests and space_map, space_stale, space_fleet, space_puppet, space_rivalry, space_contracts, space_jump, space_guest_ui, space_allies, space_diplomacy), smoke 20261028-013951 (13/13; `ui` skipped: exclusive, needs a lane 1-5).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: allies.aborted (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
