---
id: BUG-0074
title: after leaving a session, the game quits to the menu and loads the old host world once the editor closes
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#52
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: df2f0a9, 75b7383
tests: tools/tests/coop_world_queue.py (suite world_queue)
signature:
docs: 
related: #52
switch: world.world_reset
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/52 (full text, discussion).

A world that arrives while the guest is in its editor is held back until the guest leaves the editor (`g_deferred`), and any requested load waits in `g_via_menu` until the game reaches the galaxy menu. Neither is cleared when the session ends. A player who leaves the session while one is pending still gets their game ended (quit message: save, galaxy menu) and the old host world loaded, after they are no longer playing together. Severity: wrong behaviour (the solo game is quit and replaced unexpectedly).

### Where
- The deferred world and its tick: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L638-L670
- Set by `finish()` when the guest is in an editor: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L1037-L1044
- The queued load and the stage quit: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L564-L636 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L918-L931
- `guest_tick` runs whenever the role is not host, also without a session: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1103-L1106
- The session-end paths (`disconnected` callback, `do_leave`) reset other modules but have no `world::` reset: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1490-L1505 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1760-L1770

### How to reproduce
1. Two instances, Creature stage, `auto_load_world=1` (default).
2. The host starts an evolution vote; both open their editors.
3. The host leaves its editor first, so its new world is shared while the guest is still in its editor (feed: "loaded when you leave the editor").
4. The guest presses Leave (or the host quits Spore) while the guest is still in the editor.
5. The guest closes its editor and plays on alone. After about 5 s its game quits to the galaxy menu and loads the host's world.

### Expected
When the session ends, pending world loads and transfers are dropped. A player who has left keeps playing the game they are in.

## Root cause
**What happens**

1. The guest is in its own editor (the evolution vote: everybody edits its own creature) when the host's world arrives. `finish()` stores it: `g_deferred = Deferred{true, slot, planet, host_name, 0}` and tells the player "the world of X is loaded when you leave the editor".
2. The session ends while the guest is still in the editor: the player presses Leave, or the host quits.
3. world.cpp has no session-end function. `g_deferred.on` stays true.
4. `coop.cpp` keeps calling `world::guest_tick()` every frame (the role is not host). `deferred_tick` waits until the mode is not an editor for 2 s, then calls `load_slot(d.slot, ...)`.
5. `via_menu_tick` waits until the stage has been playable for 3 s, then posts the game's quit message 0x4715068 (save and leave to the galaxy menu) and loads the old host world from there.
6. The same happens without an editor when the session ends between the request and the menu: `g_via_menu` is not cleared either (up to 60 s). `g_in`, a half-received transfer of up to 128 MB, also stays allocated until the next offer.

### Confidence
CONFIRMED by reading the full path (`finish` -> `g_deferred` -> `deferred_tick` -> `load_slot` -> `via_menu_tick`, and no reset on either session-end path). Not run in the harness.

## Fix
Fixed on fix/w2-world (df2f0a9). `world::on_session_end()` (called by `end_session_modules`, so by
every session end: Leave, disconnect, a new host/join, unload) drops the guest's load of a
received world that waits for the editor (`g_deferred` with a host) or for the galaxy menu
(`g_via_menu` marked as a share), and a half-received transfer (`g_in`). The player's own
`coop.load` stays. The host's shares were already dropped by BUG-0140. Old behaviour:
`coop.worldfix world_reset 0`.

The review suggested:

Add `world::on_session_end()` that resets `g_deferred`, `g_via_menu` (only one this mod queued for a received world, not a `coop.load` the player asked for), `g_in` and the host's `g_pending` / `g_out`. Call it from the `disconnected` callback, `do_leave` and `on_unload`. Old behaviour behind `coop.fix world_reset off`.

## Verification
`coop_world_queue.py` part 4: the host leaves its editor first, its world waits on the guest ("load
after the editor"), the guest presses Leave and then "done". FAIL without the fix: the guest's game
went GalaxyGameEntry -> LoadGame (the host's world loaded after the session ended); PASS: the guest
stays in its Creature game for 20 s. Lane 3, 2026-10-28, PASS: lab runs 20261027-230646 and the branch's smoke run. FAIL runs: `coop_world_queue.py --without share_one_save,share_drop_peer,world_reset` and `--without load_in_editor` (outputs kept in the wave's scratch folder, named in the branch report).

The planned test was:

`tools/tests/coop_leave_in_editor.py` on two instances: get a world deferred on the guest while it is in the creature editor (as in `coop_editor_desync.py`), then the guest leaves the session and closes the editor. Assert with the game's own state that the guest stays in the Creature stage (mode 0x1654C01, the same avatar) for 15 s and the game's `CurrentGameName` (`SporeCoop::CurrentGameName`) does not change. It fails on main (the game goes to the galaxy menu and loads) and passes with the fix.

### Related
- Same "state survives the session end" pattern: the issue about creations `g_world_pkgs` (host creations shadowing the guest's own after leaving).
- docs/03-gameplay.md 3.2 (world sharing), docs/re/space.md "Loading a save while a stage runs".

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #52.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: the session end drops the guest's queued world loads (coop_world_queue).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: world.world_reset (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
