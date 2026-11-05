---
id: BUG-0496
title: Creature: the guest gets no heal at its home nest (split from BUG-0460)
status: open
kind: bug
stage: Creature
reported: 2026-11-03
source: player report 2026-11-02 (BUG-0460), split on fix/w40-world-objects
role: guest
versions: 
fixed_in:
branch: fix/w44-guest-player2
commits: 2ce39e6a
tests: nest_heal (coop_nest_heal.py)
signature:
docs: docs/re/creature.md#creature-nest-heal, docs/re/creature.md#creature-nest-heal-fight
related:
---

## Report
The guest's mark in BUG-0460 (s 115): 'for me nest is not extinct and i don't get any heals from it'. The extinct half is fixed in BUG-0460 (wobj.herd); the heal at the home nest was not reproduced or analysed: which nest heals the guest's avatar (its family herd's nest, or the host's) and whether the guest's game runs that heal at all is not known. Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not reproduced (fix/w41-creature-left, 2026-11-03). Observed (lab 20261103-235806, nest_heal): in the creature fixture the guest's own game heals its avatar at the home nest like the host's (3.6 -> 16.0 full in 12 s; the host 3.2 -> 10.0), and neither heals 100 m away. Inferred: the player's case is another nest state (after a migration, or a nest the guest's game sees differently: the same mark says the nest was not extinct for the guest), not the heal itself. Next: repeat the test after coop_migration's nest move.

* Observed (Ghidra, fix/w44-guest-player2): the heal is creature +0xFB8, set by the avatar branch of 0xC084B0 for a
  sensed friendly nest (0xC6ABF0) in range while health < max, food meter +0xBBC >= 1, the brain's node is not
  0x2E5E38D and no BT "Fight" event 0x60995FC of the avatar runs; applied by vt 0x88 0xC02E10 (not in a cinematic).
  The avatar's herd or "home" nest is not read (docs/re/creature.md#creature-nest-heal-fight).
* Runtime (lane 6, out/lab/20261105-111330): both players heal at the home nest before and after 30 s of combat input
  (guest 3.5 -> 16.0, host 3.4 -> 10.0); the input made no Fight event, so a lingering Fight against a proxy (inferred
  candidate) was not tested.

## Fix
Not fixed: not reproduced. `coop.rules heal` (player_rules.cpp) prints the heal's inputs on any game (the flag, health,
food, cinematic, BT node, running Fight events) for the next report or test.

## Verification
no-fail-run: not reproduced (out/lab/20261105-111330); nest_heal stays the regression guard.

## History
- 2026-11-03: opened.
- 2026-11-03: not reproduced on fix/w41-creature-left (both players heal at the fixture's nest); test nest_heal added.
- 2026-11-04: third look on fix/w42-creature-left: an allied species' nest (record ally 6) heals neither player (host 3.0 -> 3.0, guest 3.0 -> 3.0), the home nest heals both also with the world's species (0.7.5 final lab 20261104-061516). Still not reproduced; not fixed. docs/re/creature.md#creature-leftovers-3.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): not fixed. New: the friendly-nest test 0xC6ABF0 (own species, herd +0x191 mbBestFriends, +0x190, relation 4), read by 0xC084B0 / 0xD401E0 / 0xD41A20 (~ the nest heal and sound), needs +0x191 like the minimap: a record written by hand (the w42 test) sets no +0x191, which explains "an allied nest heals neither player" there; a guest's herds get +0x191 now (social.nest_ally, BUG-0561). The home nest heal itself still not reproduced.
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): the avatar's heal found (Ghidra: +0xFB8, 0xC02E10, the Fight event gate); not reproduced, not fixed.
