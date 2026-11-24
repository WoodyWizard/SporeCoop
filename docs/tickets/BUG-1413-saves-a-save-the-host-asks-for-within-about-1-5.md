---
id: BUG-1413
title: Saves: a save the host asks for within about 1.5 s after a world-transfer save is dropped by the game and not retried
status: fixed
kind: bug
stage: All
reported: 2026-11-21
source: found on the way: BUG-1398 (lab 20261121-003202)
role: host
versions: 0.7.16, 0.7.16.1
fixed_in: unreleased
branch: fix/w63-net
commits: 0b6e70d2
tests: saved_box_player, saved_box_player_off, saved_box_twice_off (still FAILs: the mod's own dropped posts are not made again while world.save_allowed is off)
signature:
docs: docs/re/creature.md#creature-save-dropped, docs/known-issues.md#issue-save-quit-dropped
related: BUG-1365, BUG-1398
---

## Report
Found on the way by fix/w62-saves (BUG-1398): the game's Save handler (0xB29960) silently drops a save that
comes while the previous one is not finished (BUG-1365). Since BUG-1365 (`world.save_allowed`) the mod refuses such a
save with its reason and retries it for the world transfer and the autosave, but a save the host player asks for
itself (the pause menu, a quit with save) within ~1.5 s after a world-transfer save for a joining guest is still lost
without a word. In the lab it hid in `twoinst.session_restart` (store writes 2 -> 2).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (BUG-1365, disassembly of 0xB29960): the game's Save handler returns at once for 0x1CD20F0 while
SaveGame_Clock runs or SaveGame_Allowed is 0 (cleared by every save, set again 1500 ms after its box's OK). The mod's
own saves wait for it since BUG-1365 (world::save_may_start), but the pause menu's Save posts the same message from
the game's UI, past the mod: right after one of the mod's saves it is dropped with no box and no trace.
(asm 2026-11-22: the handler is thiscall bool(listener, uint32 message, void* data) ret 8, prologue without relative
instructions: `SporeCoop::SaveGame_MessageHandler` in overrides_dc04aee5.sym.)

## Fix
world.cpp hooks `SporeCoop::SaveGame_MessageHandler` (it only looks, the game's code runs as before): a Save the game
will drop now (save_may_start false) is remembered, and resave_tick posts it again through world::save_game (its
blockers, the box watch) as soon as the game takes one: every 250 ms, for up to 30 s, at most 5 times per request; not
when the game left the mode it was asked in (a quit right after it: not saved in the galaxy menu; logged, known issue
#issue-save-quit-dropped). A dropped post of the mod's own is left alone while `world.save_allowed` is off, so the
BUG-1365 twin still shows its bug. `coop.save box` counts dropped= reposted= lost=; `coop.save asplayer` (tests) posts
the Save message as the pause menu's button. Switch `coop.fix world.save_retry off`: dropped (0.7.16).

## Verification
Open, waiting for the lab run:
`tools/lab.py run saved_box_player saved_box_player_off saved_box_twice saved_box_twice_off`
* saved_box_player must PASS (the player's save inside the 1.5 s: dropped by the game, made again, written in 20 s);
* saved_box_player_off must FAIL (SPORE_FIX_OFF=world.save_retry: nothing written);
* saved_box_twice must PASS and saved_box_twice_off must still FAIL (BUG-1365 unchanged).

- 2026-11-23 (0.7.16.1 final check): saved_box_player PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-21: opened.
- 2026-11-22 (fix/w63-net): the dropped Save is seen in the game's handler (hooked) and made again once the game takes
  one (world.save_retry); lab case saved_box_player and its twin.
