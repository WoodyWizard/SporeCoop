---
id: BUG-0157
title: a queued world load can run under an editor that opened after the world arrived
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#136
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: df2f0a9, 75b7383
tests: tools/tests/coop_world_queue.py (suite world_queue)
signature:
docs: 
related: #136
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/136 (full text, discussion).

A guest's received world is loaded through the galaxy menu once the stage has settled. When an editor opens after the load was queued but before the stage is left, `via_menu_tick` sees the editor as an "other" mode and starts `LoadGameByName` straight from it. `world::finish` explicitly avoids this ("loading a world under an open editor crashed the game") only for an editor that is already open when the world arrives. Severity: low (needs a timing window); a crash when it happens.

### Where
- The editor counts as "other" and loads at once: [world.cpp#L583-L609](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L583-L609) (`bool other = ...` at L585).
- The request waits for the settle, or for the quit to be handled, while in a stage: [world.cpp#L610-L634](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L610-L634).
- The protection exists only at arrival: [world.cpp#L1037-L1043](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L1037-L1043).
- The guest's own editor after a vote checks `mode_switch_pending()` but not a queued world load: [coop.cpp#L1044-L1087](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1044-L1087) (check at L1059 and L1071).
- `h_Submit` lets a guest's own editor request through at once: [coop.cpp#L697-L728](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L697-L728).

### How to reproduce
Timing dependent, 2 players, Creature:
1. Host and guest play.
2. Host: `coop.share 1` (or come back from an editor) so the guest receives a world.
3. Guest: right after "loading the world of ..." appears, and before the screen goes to the menu, open the creature editor at the nest (or run `coop.vote editor` on the guest).

Easier to hit with a short stage time (the guest just came back from its own editor, so the settle time is still running). Watch the guest's log for "world: loading from Editor".

### Expected
A queued load never runs inside an editor. It waits until the player is back in a stage, like `g_deferred`, and no editor is opened (by the mod) while a world load is queued.

## Root cause
**What happens**

1. The guest receives a world while in a stage. `finish` sees no editor and calls `load_slot`, which sets `g_via_menu.on`.
2. `via_menu_tick` waits:
   - until the stage has been playable for `kStageSettle` (3 s, longer right after a load or a return from an editor: `deferred_tick` loads 2 s after the editor, so it waits about one more second);
   - or, once settled, it has posted 0x4715068, which the game handles about 1 s later (while a mode switch is pending, the game posts it again).
3. In that window an editor opens:
   - the guest's own editor after a vote (`own_editor_tick`; its task checks `mode_switch_pending()`, which is not yet set by a posted, unhandled quit);
   - or the player opens one (Creature nest, Cell mating, Civ or Space design editors).
4. The next `via_menu_tick` finds `mode == kEditor` (or `kEdit`): `other` is true, so it runs `load_now` outside the frame with the editor open. This is the case `finish` guards against.

### Confidence
PLAUSIBLE. The code path was confirmed by reading and by a second reviewer. The timing window and the crash itself were not reproduced (the crash is the one described in the comment in `finish`).

## Fix
Fixed on fix/w2-world (df2f0a9). `via_menu_tick` treats an editor (kEditor/kEdit) like `finish`
does: the queued load moves to `g_deferred` (loaded 2 s after the editor is left) instead of
`load_now` from the editor ("loads held" in `coop.status`). `own_editor_tick` and its outside-frame
task wait while `world::load_queued()`. Old behaviour: `coop.worldfix load_in_editor 0`.

The review suggested:

- In `via_menu_tick`, treat kEditor/kEdit like `g_deferred`: move the request to `g_deferred` (load 2 s after the editor is left) instead of loading from it.
- Make `own_editor_tick` (and its outside-frame task) skip while `g_via_menu.on || g_deferred.on`.
- Keep the old behaviour behind `coop.fix load_in_editor off`.

## Verification
`coop_world_queue.py` part 3: both players in their editors after an evolution vote, the guest
queues a load (`coop.load` of its received slot, the same queue a received world uses). FAIL without
the fix: "loading from Editor", the game left the editor and the guest logged 174 caught faults; PASS:
the guest stays in its editor, the load waits (and is replaced by the host's world). Lane 3, 2026-10-28, PASS: lab runs 20261027-230646 and the branch's smoke run. FAIL runs: `coop_world_queue.py --without share_one_save,share_drop_peer,world_reset` and `--without load_in_editor` (outputs kept in the wave's scratch folder, named in the branch report).

The planned test was:

`coop_world_during_editor.py` (two instances, Creature fixture):
- Host `coop.share`.
- The guest opens its editor through the game's request (EditorRequest Submit, outside the frame, as the real game does, not inside Present) within 0.5 s of the received world being queued (wait on the status line "world: received").
- Assert that the guest's game (`GetActiveModeID`) is in the editor until it is closed, that no load starts while it is open (no "loading from Editor" in the log), and that after closing it the guest reaches the host's world and no crash report was written.

It fails on main when the window is hit (repeat 10 times) and with `coop.fix load_in_editor off`; it passes with the fix.

### Related
#52 (a world load after leaving the session, once the editor closes), #74 (the test command that submits editor requests inside Present), docs/re/creature.md "Saves made while an editor opens".

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #136.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: a queued load waits for the editor to close; the own editor waits for a queued load (coop_world_queue).
- 2026-10-28: released in 0.7.3.0.
