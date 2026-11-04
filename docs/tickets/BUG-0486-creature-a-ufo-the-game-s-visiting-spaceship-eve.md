---
id: BUG-0486
title: Creature: a UFO (the game's visiting spaceship event) exists in one game only
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in: 0.7.5
branch: fix/w41-infra
commits: 9befc502
tests: drama_guest
signature:
docs: docs/re/creature.md#creature-drama-events
related:
---

## Report
Detector: `Creature:noun_only_host:ufo` 2% of Creature comparisons, `noun_only_guest:ufo` 1%, `guest_only:ufo` once near the guest. The game's own random event (a spaceship visiting / abducting) runs in each game separately: one player sees it, the other does not. Class A (events not mirrored).
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (Ghidra, GOG 2024): the UFO visit is cUFOAbductionEvent, kind 2 of the game's Drama Manager (a singleton,
0xD51610), which every game runs by itself in Creature and Tribe; its start spawns a type 7 ship over the own avatar
(docs/re/creature.md#creature-drama-events). Observed (lab, out/lab/20261104-033630, switch off): `coop.drama start
2` (the manager's own StartKind) on the guest makes a UFO in the guest's object list (0 -> 1) and its event
active, the host has none: the detector's noun_only_guest / guest_only:ufo. The host's start makes the host's UFO
(0 -> 1) and the guest never sees it (noun_only_host:ufo).

## Fix
Partial. mods/coop/drama.cpp hooks `SporeCoop::DramaManager_StartEvent` 0xD4D4E0: on a guest in a session an event
of a kind in the mask (default kind 2, the UFO) is not started (stays inactive; the manager tries again after
its next wait). So the guest's game no longer has a UFO of its own that takes its mirrors of the host's
creatures. Switch `coop.fix creature.guest_events off`; `coop.drama [start <kind> | mask <hex>]`.
Not done: the host's UFO shown on the guest (a mirror of the ship moved by the host's position, its BT off):
the noun_only_host:ufo row stays until then.

## Verification
coop_drama_guest.py (suite drama_guest, creature fixture): the guest's StartKind 2 makes no UFO in its object
list and the event stays inactive; the host's makes one (control).
- FAIL with SPORE_FIX_OFF=creature.guest_events: out/lab/20261104-033630 (guest 0 -> 1 UFO, event active)
- PASS: out/lab/20261104-033725 (guest 0 -> 0, refused 1, active 0; host 0 -> 1)

## History
- 2026-11-03: opened.
- 2026-11-04: the guest's own UFO held back on fix/w41-infra (creature.guest_events); the host's UFO on the guest not done.
- 2026-11-04: released in 0.7.5.
