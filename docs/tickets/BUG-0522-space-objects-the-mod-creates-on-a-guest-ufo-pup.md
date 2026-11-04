---
id: BUG-0522
title: Space objects the mod creates on a guest (UFO puppets of the partners) go into the guest's saves
status: fixed
kind: bug
stage: Space
reported: 2026-11-03
source: split from BUG-0495 (fix/w41-tribe-world)
role: guest
versions: 
fixed_in: 0.7.6
branch: fix/w42-space-life
commits: 1d839bae
tests: space_save_strip
signature:
docs: docs/re/space.md#space-save-strip
related: BUG-0495, BUG-0470, BUG-0020
---

## Report
Split from BUG-0495 (its Tribe half fixed on fix/w41-tribe-world, `tribe.save_strip`). Still open: the Space
objects the mod creates on a guest: the partners' UFO puppets (`space_spawn` in modes.cpp: SpawnUFO, a Defender
of the player's empire, `g_ufo_ident`) and the fallback puppet in the avatar's herd (modes.cpp creature_spawn when
the partner's species is unknown: savestrip's owner rule keeps a puppet in the avatar's herd). Not checked whether a
Space save writes the system's UFOs at all, nor which lists hold a UFO (the null rule of BUG-0470: a stripped
reference read back as null where the game reads a pointer). Needs a Space save + reload test as
tribe_save_strip / civ_save_strip.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (lab 20261104-094503, before the fix; 20261104-095242 with `SPORE_FIX_OFF=space.save_strip`): a guest
  (Carol, lane 5) in the `space` fixture's star system with the host's and Bob's puppets and one mirror of a host
  Defender saved with the game's own save, was restarted alone and loaded it: the world had 3 type 6 Defenders of
  its own empire, two exactly where the puppets stood, no longer invincible. A Space save writes every UFO of the
  noun list; the savestrip filters knew creatures, herds, Civ vehicles and Tribe objects only.
* Observed: with null references for the mod's ships the reload ran 10 s with no fault; ModAPI's
  `cSimulatorSpaceGame` +0x78 (mNPC_UFOs) was an empty vector there, so no list was found that reads a null back
  (BUG-0470's rule). Inferred, not checked: the same on a planet (Space saves only off-planet, 15.4).
* Inferred from the code: the host's share save holds the guests' puppets the same way (the strip runs on both
  roles; the host's log of the test shows `1 ships of ours leave this save` for Bob's puppet when Carol joined).
* Not done: the Creature-stage fallback puppet in the avatar's herd (modes.cpp creature_spawn, the partner's
  species unknown) named in the report; savestrip's owner rule still keeps it (a null member of the pack faults).

## Fix
`space.save_strip` (squadron.cpp): a savestrip filter writes a null reference for every listed UFO that is a
partner's puppet (`puppet_owner`), a guest's mirror of a host ship (`g_mirror_of`) or a retired puppet; the set is
made in a save hook (savestrip::add_save_hooks) when the save starts writing. Both roles. `coop.fleet list` shows
`save_stripped=<refs>/<saves>`, and each ship's `inv=`, `model=` and `puppet` (the test tells our ships by them).

## Verification
* FAIL (`SPORE_FIX_OFF=space.save_strip`): out/lab/20261104-095242: the reloaded world has 2 Defenders where the
  puppets stood (and one more of the mirror's type away from it).
* PASS: out/lab/20261104-094827, out/lab/20261104-095047: no ship of ours in the reloaded world, no fault; the
  puppets and the mirror stay in Carol's session after the save.

## History
- 2026-11-03: opened.
- 2026-11-04: fixed on fix/w42-space-life (space.save_strip), test space_save_strip.
- 2026-11-04: released in 0.7.6.
