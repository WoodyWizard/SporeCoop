---
id: BUG-0166
title: a world share for a guest who already left stays queued, so the host saves for nobody and its editors wait up to 15 s
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#145
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: df2f0a9, 75b7383
tests: tools/tests/coop_world_queue.py (suite world_queue)
signature:
docs: 
related: #145
switch: world.share_drop_peer
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/145 (full text, discussion).

When a guest leaves before the host's save for its world share is written, its `PendingShare` entry stays in `world::g_pending`. Until the host can save, it keeps saying "a player is waiting for the world: ..." for a player who is gone. Meanwhile every editor request of the host is held for up to 15 s (`share_save_due`). When a save becomes possible, the host's game is saved for nobody (with the "game saved" box) and the whole slot is read for a transfer to a peer that no longer exists. Severity: low.

### Where
- The entry is queued per peer id: [world.cpp#L364-L372](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L364-L372).
- It is processed only by `world::tick`, which never checks that the peer is still in the session: [world.cpp#L686-L744](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L686-L744).
- `player_leave` does not drop it: [coop.cpp#L1707-L1717](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1707-L1717).
- The host's editor requests wait while any share is due: [coop.cpp#L600](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L600), [coop.cpp#L612-L631](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L612-L631), [coop.cpp#L720-L723](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L720-L723).
- Saves are refused, for example, in Space on a planet: [world.cpp#L333-L339](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L333-L339).

### How to reproduce
2 players, Space:
1. Host: in Space, land on a planet and host.
2. Guest: join. The host shows "a player is waiting for the world: leave the planet to save".
3. Guest: leave the session.
4. Host: open the ship or captain editor. It opens only after about 15 s (`coop.vote`: "editor held for a save N" grows).
5. Host: leave the planet. The game saves at once with the "game saved" box, and the log shows a world share to the player id that left.

### Expected
A guest that leaves takes its pending shares and transfers with it. No save, message or editor delay remains for it.

## Root cause
**What happens**

1. A guest joins while the host plays. `player_join` -> `share_with(id, err, true)` -> `g_pending.push_back({id, 0, true})`.
2. The save cannot happen yet (Space on a planet, the host in a loading screen or the menu, an editor opening), or it is posted and not yet written. The guest leaves (or its join fails and it times out).
3. `player_leave` erases the remote, but the entry stays. `world::tick` keeps retrying every 2 s. `save_pending()` stays true, so `share_save_due()` is true.
4. Alone in the session, the host's own editor request goes into `hold_submit` (L720) and is released by `submit_tick` only after the save is written or 15 s have passed. Every editor the host opens in that time (creature, building, ship) waits up to 15 s.
5. Once a save is possible, `save_game` posts a real save of the host's game. Its box pauses the game until `box_task` confirms it. `share_now` then reads the whole slot into memory for the departed peer. `tick` marks the transfer done when `send_reliable_to` fails, unless a new guest already got that id: then the new guest gets this transfer as well as its own (`share_now` replaces it only if a newer share for the same id comes after).

### Confidence
CONFIRMED by reading. A second reviewer found it while verifying related findings. Not run in a game.

## Fix
Fixed on fix/w2-world (df2f0a9). `world::tick` (host) drops every pending share and marks every
transfer done whose player is no longer in `s.players()` (logged, counted as "shares dropped"), so
no save, no editor hold (`share_save_due`) and no slot read stay for a player who left. Old
behaviour: `coop.worldfix share_drop_peer 0`.

The review suggested:

- In `player_leave` (host), call a new `world::drop_peer(id)` that erases `g_pending` entries and marks `g_out` transfers of that id done.
- In `world::tick`, also skip entries whose peer is no longer in `s.players()`.
- Keep the old behaviour behind `coop.fix share_drop_peer off`.

## Verification
`coop_world_queue.py` part 2 forces the state: `coop.worldfix queue 9` queues a share with a save for
a player id that is not in the session (as a guest that left before its save). FAIL without the
fix: a save of the host's game is posted for it; PASS: dropped at once, no save. Lane 3, 2026-10-28, PASS: lab runs 20261027-230646 and the branch's smoke run. FAIL runs: `coop_world_queue.py --without share_one_save,share_drop_peer,world_reset` and `--without load_in_editor` (outputs kept in the wave's scratch folder, named in the branch report).

The planned test was:

`coop_join_leave_share.py` (two instances, Space fixture, host on a planet):
- The guest joins and leaves within 2 s.
- The host opens an editor through the game's own request outside the frame.
- Assert the host's game (`GetActiveModeID`) is in the editor within 5 s, and that no save of the host's slot is written after the host leaves the planet (the slot's file time does not change).

It fails on main (about 15 s, and a new save) and with `coop.fix share_drop_peer off`; it passes with the fix.

### Related
#11 (state cleanup subsets), #54 (a new player inherits what the previous holder of its id had), and the issue about a host that leaves during a share save, which has the same root (shares outliving their peer or session).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #145.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: shares of players who left are dropped (coop_world_queue).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: world.share_drop_peer (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
