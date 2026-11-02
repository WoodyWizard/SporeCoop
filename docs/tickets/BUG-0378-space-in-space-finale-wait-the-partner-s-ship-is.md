---
id: BUG-0378
title: space: in space_finale_wait the partner's ship is shown about 20.7 m from where it is (sync-check p0 apart)
status: fixed
kind: bug
stage: Space
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
role: 
versions: 
fixed_in: 0.7.3.4
branch: fix/w31-space-gate
commits: 14bb610, f445dfd, 503873b, fd155ea
tests: space_finale_wait (coop_space_finale_wait.py), space_drawn, space_drawn_planet (coop_space_drawn.py), net_tests test_diag_compare
signature: sync-check MISMATCH (Space 80820000) p0 20.7 / p1 29-31 apart; Space 00000000 p0 missing, p<id> extra, s missing
docs: docs/re/space.md#space-pinned-ship-drawn, docs/06-testing.md#testing-space-drawn, docs/REPORTING.md#reporting-support-sync-check
related: BUG-0190, BUG-0388
---

## Report
`lab.py gates`: space_finale_wait 4 of 4 passed runs, `p0 20.7` apart; campaigns have 25 results with Space p apart and 12 with p missing/extra. Related: BUG-0190.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Three causes behind the Space sync-check lines:

1. **Real offset in the galactic core (observed).** The sync-check digests are read in
   `g_sess.update()`, before any drive in the mod's frame: the pose the game's update made of our
   last write, i.e. what was drawn (coop.cpp on_frame order). The UFO controller update 0x1062CF0
   runs for puppets too and moves every ship to mNextPosition + mOffsetFromPosition (+0x740); in the
   core 0x105E270 keeps the offset at (0, 0, 30 * min(1, r / 400)) and pulls next to its own orbit
   (Ghidra, GOG 2016 = GOG 2024 addresses). Players fly at r 275.5, z 20.7 (traces); the puppet
   pinned to that position got the offset again: drawn 20.7 m above (r >= 400: 30 m). Runtime with
   `coop.fleet track` (drawn rows at the top of the frame, session clock): the host's still ship drawn
   on the guest 21.0 m off (median, 49 of 49 rows) with SPORE_FIX_OFF=space.puppet_pin (lab
   20261101-095432). Taking the offset out of the pin alone was not enough (lab 20261101-094853:
   p90 17.4 m, the core's orbit pull moves next in the same update), hence the controller skip.
2. **Lag misjudged at Space speeds (observed).** The guest's ship orbiting at 207-235 m/s was drawn
   on the host 28.6 m (median) behind its own position at the same session time but 0.03 m from its
   path (lab 20261101-095432): ~0.13 s lag. The fixed 10 m for players flagged such ships (`p1 29-31`
   in the core; campaign planets 10-30 m: the host flew ~60 m/s average by its trace positions,
   inferred to be the same lag).
3. **No place (observed in the recorded logs).** Every `p0 missing, p<id> extra` and all but two
   `s missing` MISMATCH lines were in world 0 (the galaxy view): no puppet is shown and no ship
   mirrored there, each game had only its own ship. (The two others: space_mirror_model's ship
   without a model on the guest, made on purpose, BUG-0323.)

In the start system and on the fixture planet puppets and mirrors were drawn within 0-3 m of their
path with and without the pin fixes (coop_space_drawn.py, lab 20261101-093823, -094339): the offset
is 0 there.

## Fix
* `space.puppet_pin` (modes.cpp space_pin + squadron.cpp hook of SporeCoop::UFOController_Update,
  GOG 2024): a partner's ship pinned this frame skips the controller update (no velocity, stays where
  the mod put it); next / destination = the remote position minus the ship's offset, zoom altitude
  their length (what SnapToDestination would leave). `fleet.mirror_pin` does the same for the host
  ships' mirrors (BUG-0190).
* `sync.speed_slack` (diag_sync.cpp): in Space a player's limit grows by its speed x (0.2 s + the
  guest's round trip [+ half the owner's]), at most 1 s / 300 m.
* `sync.no_place`: no players / ships compared in world 0.
* Tests' instrument: `coop.fleet track`.

## Verification
* FAIL: 20261101-092530 (main code: space_finale_wait failed by the log gate, `sync-check: MISMATCH
  ... p0 20.7`); 20261101-095432 (SPORE_FIX_OFF=space.puppet_pin: `core: the host ship on the guest`
  drawn 21.0 m off, FAIL, and the gate).
* PASS: 20261101-100710 (space_finale_wait, space_coop, space_play x3 each, gates on, 9/9; the host
  ship on the guest 0.0 m); 20261101-100032 (the host ship 0.0 m, no MISMATCH; it failed then on the
  other direction's check, since printed only: BUG-0388).
* Unit: net_tests test_diag_compare (no place, speed slack).

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w31-space-gate: fixed (controller skip + offset pin, sync-check speed slack and no place).
- 2026-11-02: released in 0.7.3.4.
