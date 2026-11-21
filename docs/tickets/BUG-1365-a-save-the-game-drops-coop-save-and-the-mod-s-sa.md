---
id: BUG-1365
title: A save the game drops: coop.save and the mod's saves report 'saved' while the game's Save handler ignores the message (its last save still finishing), own_editor_drop 'not saved'
status: open
kind: bug
stage: All
reported: 2026-11-20
source: final check 20261120-133703 (own_editor_drop)
role: host
versions: <=0.7.16
fixed_in:
branch: fix/w61-creature-a
commits: 3ffe3266
tests: saved_box_twice, saved_box_twice_off, own_editor_drop
signature: coop.save answers "saved (mode ...)" and no file of the slot is newer than the request (test: ['not saved'])
docs: docs/re/creature.md#creature-save-dropped
related: BUG-1298, BUG-0846, BUG-0276
switch: world.save_allowed
---

## Report
Final check of 0.7.16 (lab 20261120-133703, lane 3): own_editor_drop passed every check of the guest's own outfitter
and the rejoin, then "FAIL the host's save names no key of the guest's (['not saved'])": `coop.save` answered
"saved (mode 0x01654C02)" and no file of the host's slot Game0 was newer than the request in 90 s. The same test passed
in the first final check (20261120-041304) and in 20261119-164857 / -214823.

## Root cause
Observed (own_editor_drop.host.game.log): the guest's rejoin made the host save for the world share at s 72.8 (box up
and confirmed by the mod at s 73.54, "the save for player 1 was written" s 74.29). The test's checks after the join
passed at once (the guest was still in its own copy of the world, the share's reload followed at s 74.9-79.7) and its
`coop.save` came within ~1.5 s of that box: no "Tribe, saving" hitch, no box, nothing written. In the passing run
20261120-041304 the test's save came 10 s after the share save and was written (hitch at s 86.8).
Observed (objdump of the GOG 2024 exe, docs/re/creature.md#creature-save-dropped): the game's Save handler 0xB29960
returns at once for 0x1CD20F0 while SaveGame_Clock runs or SaveGame_Allowed is 0 (cleared by each save, set again
1500 ms after its box's OK). `world::save_game` posted the message without looking and reported success, so
`coop.save`, the world shares (BUG-0276's settle wait does not cover this), the autosave and the evolution's save
could all be dropped silently right after another save.

## Fix
`world.save_allowed` (world.cpp `save_may_start`): `save_game` refuses a save while the game would drop it, with the
reason "the previous save is still finishing"; the share loop and the autosave try again, `coop.save` says it, and
`coop.save box` shows `may_start` / `too_early`. Off: posted as before (0.7.16). own_editor_drop asks again until the
host's game takes its save (a refused one is printed); a "saved" that writes nothing still fails it.

## Verification
Open, waiting for the lab run (no lab runs today, owner rule):
`SPORE_DETECTOR=20 python3 tools/lab.py run saved_box_twice saved_box_twice_off own_editor_drop --lane N --lane M --retry 0 --no-build`
* saved_box_twice must PASS (the second save right after the box: refused for now, taken within 5 s, written);
* saved_box_twice_off must FAIL (`SPORE_FIX_OFF=world.save_allowed`: "saved" and nothing written);
* own_editor_drop must PASS.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-creature-a): root cause by the logs and the disassembly; fixed with world.save_allowed, new
  case coop_saved_box.py --case twice with its twin; own_editor_drop retries a refused save.
