---
id: BUG-0140
title: a host that leaves a few seconds after a share save can no longer leave any stage (quit message re-posted forever)
status: fixed
kind: freeze
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#119
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-core
commits: fad94f0, 066df15
tests: tools/tests/coop_leave_during_share.py (suite leave_during_share)
signature:
docs:
related: #119
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/119 (full text, discussion).

If the host leaves the session (Leave, or Join of another game) in the 2.5 to 10 s after the save for a world share was posted, the share entry stays in `world::g_pending` for good. `world::save_busy()` then returns true forever, and the hook on `Simulator_HandleMessage` re-posts every quit message 0x4715068. Leaving a stage for the galaxy menu stops working: `coop.load`, the load of a received world as a guest of another host, and the game's own exit to the menu, which goes through this message (docs/re/space.md 8.2). Hosting again releases it. Severity: medium-high.

### Where
- `save_busy()` is true while any pending share has `need_save == false`: [world.cpp#L383-L391](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L383-L391).
- The entry is set to `need_save = false` after `save_game` and removed only by `world::tick`: [world.cpp#L686-L744](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L686-L744).
- `world::tick` runs only while the role is Host (the frame and the keepalive): [coop.cpp#L1103-L1106](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1103-L1106), [coop.cpp#L918-L926](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L918-L926).
- `do_leave` and `do_join` do not clear world's queues: [coop.cpp#L1752-L1771](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1752-L1771).
- The re-post: [coop.cpp#L679-L695](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L679-L695).

### How to reproduce
2 players, any stage (Creature is easiest):
1. Host: in a stage, host a session.
2. Guest: join. The host logs the share save ("a player is waiting" / the saved box).
3. Host: within about 3 s of the save, press Leave in the co-op window (or `coop.leave`).
4. Host: pause menu, exit to the main menu (or `coop.menu`, which posts 0x4715068): the game stays in the stage, and `coop.vote` shows "quit held for a save N" growing.
5. Variant: after step 3, join another host whose world arrives: it is never loaded.

### Expected
Leaving the session drops the shares that were waiting for it, and `save_busy()` describes only a save that is really being written (it should also expire, like its second condition does after 10 s).

## Root cause
**What happens**

1. A guest joins, or the host comes back from an editor or starts a new stage. `share_with(peer, err, true)` queues a `PendingShare` with `need_save = true`.
2. `world::tick` posts the save. The entry becomes `need_save = false`, `at = now + 2500`, `latest = now + 10000`, and it is shared and erased once `at` has passed and the saved box is seen (or `latest` has passed).
3. Before that, the host leaves the session (`coop.leave`, the Leave button, or Join of another game, which runs `Session::join` -> `leave("restart")`). The role becomes None (or Client), so `world::tick` is never called again. Nothing else erases `g_pending`.
4. `save_busy()` now always finds the entry with `need_save == false`. Every 0x4715068 that reaches `h_SimMsg` is posted again and swallowed (`return 1`), with "leaving the stage waits until the world save is written" in the log every 5 s.
5. Results:
   - The player stays in the stage when the game uses this message to leave it.
   - `coop.load` does nothing.
   - As a guest of another host, the received world's `load_slot` posts the quit, which never arrives; after 60 s it gives up with "the game did not go to the galaxy menu", while the message keeps going round.
   - Hosting again runs `world::tick`, which handles the stale entry and frees it.

### Confidence
CONFIRMED by reading. A second reviewer verified it independently. Not run in a game. That the pause menu's own exit uses 0x4715068 is inferred from docs/re/space.md 8.2 ("the native way out"); `coop.menu` and the world loads use it for sure.

## Fix
Fixed on fix/w1-core (fad94f0, 066df15). `world::reset_shares()` clears the pending shares and the
outgoing transfers; `end_session_modules()` calls it, so every session end (Leave, Join elsewhere,
host gone, re-host, unload) drops them. `save_busy()` also counts a written-but-unshared entry only
up to 5 s after its `latest`. `coop.status` lists pending shares ("share for N waits for its save" /
"saved, being written"). Old behaviour: `coop.corefix share_reset 0`.

The review suggested:

- Add `world::reset_shares()` (clears `g_pending` and `g_out`, resets `g_saved_at`) and call it whenever the role changes: `do_leave`, `do_host`, `do_join`, `on_unload`.
- Also bound `save_busy()` in time: an entry whose `latest` has passed by more than a few seconds no longer counts.
- Keep the old behaviour behind `coop.fix share_reset off`.

## Verification
Two-game test `coop_leave_during_share.py` (creature fixture, lane 2, 2026-10-28): the guest rejoins,
the host leaves 0.2 s after "saved, being written" shows, then `coop.menu`.
FAIL with `--without-fix`: after Leave `coop.status` still showed "share for 1 saved, being written",
the game stayed in the stage for 40 s and the quit was held 9 times. PASS with the fix, 2 runs: the
queue is empty after Leave and GalaxyGameEntry is reached after 2 s (quit held 0 times).

The planned test was:

`coop_leave_during_share.py` (two instances, Creature fixture):
- The guest joins; the host waits for the share save to be posted (status line), then runs `coop.leave` within 1 s.
- The host then runs `coop.menu`.
- Assert from the game's own state that the host's game reaches the galaxy menu (`GetActiveModeID` == GalaxyGameEntry) within 15 s.

It fails on main (the mode stays in the stage, the quits-delayed counter grows) and with `coop.fix share_reset off`; it passes with the fix.

### Related
#11 (leaving, re-hosting and hot reload clean up different subsets of the state). This issue is one concrete, severe consequence of that gap, with its own trigger. #52 (stale world load after leaving). docs/re/creature.md 9 (why `h_SimMsg` re-posts the quit).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #119.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w1-core: shares dropped at every session end (coop_leave_during_share.py).
- 2026-10-28: released in 0.7.3.0.
