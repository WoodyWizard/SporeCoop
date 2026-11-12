---
id: BUG-0941
title: Space: a connected guest in Space does not follow the host's load of another Space save (it keeps playing its copy of the old one)
status: fixed
kind: bug
stage: Space
reported: 2026-11-11
source: fix/w47-final12b, first draft of coop_space_save_change.py (lab 20261111-105829, 20261111-105831)
role: guest
versions: 0.7.12
fixed_in: unreleased
branch: fix/w48-space
commits: 82221ae4
tests: space_load_follow
signature:
docs: docs/re/space.md#space-s1-0713, docs/07-modes.md#modes-host-save-follow
related: BUG-0933, BUG-0709
---

## Report
Seen while writing BUG-0933's test (first draft: both games in the space fixture, then the host loads a copy of that
save from another slot, `coop.load Game9 Otherspace`; lab 20261111-105829 and 20261111-105831). The connected guest
did not follow the host into the loaded save: its log has no "receiving world" until it left and joined again
(then: "the host plays Madas: loading Madas.spo"). Until then it kept playing its copy of the old save in the same
star system, the host's ship shown there ("Alice appeared in the world"), and with equal save names the shared money
went on between two different saves.

## Root cause
Not analysed. Observed only: a host's load from Space into another Space save sends no world to a connected guest
(in the gated stages a guest elsewhere is pulled into the host's world). Also observed there: the save name the mod
uses in Space is the file's name until the first save (the copied file `Otherspace.spo` gave world 32DFFED8; after
the host's save for a share the name was `Madas.spo` again, 33DE75F6: "the game's save name is now 'Madas.spo'"),
so BUG-0709's "another Space save" can change in the middle of a session for a renamed copy.
With BUG-0933's fix the guest hears the host's new save (MSG_HOST_WORLD) and holds the shared money while it
plays the old one, which is the right answer for two different saves; whether the guest should be taken along is
the open question.

## Root cause (2026-11-12, fix/w48-space)
Observed (code, confirmed by the FAIL run): coop.cpp on_mode_change shares the world with the guests only on a new
stage or after an editor (`new_stage || evolved`). A host's load from the galaxy menu into Space while it was in Space
before is neither (the stage is the same), and in Space the state's world is the place (star, planet), the same star
in both saves, so follow_phase saw every guest in the host's world. The feed line "the others play on alone until you
load a game again (they get its world)" promised a share that never came in Space.

## Fix
Decision: the guest follows (as in the gated stages). The host remembers its save file (`world::current_game_key`: the
slot folder and the planet file, lower case) when it enters Space and when it leaves Space for the menu; when it comes
into Space from a load with another file, every guest gets the world (`needs_world`, as for a new stage), with the
notice "<host> loaded another game: everybody follows". The same file loaded again shares nothing (the guests' copy is
that game). This also covers two slots with the same planet name, which the save-name check of BUG-0933 cannot tell
apart. Switch `coop.fix space.load_follow off` (0.7.12: no share; BUG-0933 then holds the shared money). `coop.status`:
`space save: <file>, load shares N`.

## Verification
`coop_space_load_follow.py` (suite `space_load_follow`): both in the space fixture, a copy of the fixture into the
host's slot Game9 (the same planet name), the host goes to the galaxy menu and loads Game9.
* FAIL with `SPORE_FIX_OFF=space.load_follow`: lab 20261112-034009 (the guest's game never left Space, it played
  `...\games\game1\madas.spo` before and after; host `load shares 0`).
* PASS: lab 20261112-034012 (the guest's game went to the galaxy menu and loaded the received world, now
  `...\games\game2\madas.spo`; host `load shares 1`; the guest in the host's world).

## History
- 2026-11-11: opened.
- 2026-11-12: fixed on fix/w48-space (the host shares its world when it loads another Space save; switch space.load_follow).
