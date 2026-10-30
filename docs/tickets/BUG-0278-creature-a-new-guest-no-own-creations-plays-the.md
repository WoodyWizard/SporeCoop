---
id: BUG-0278
title: Creature: a new guest (no own creations) plays the legless creature from the start of the stage and cannot change it
status: fixed
kind: bug
stage: creature
reported: 2026-10-29
source: Discord report 2026-10-29
role: guest
versions: 0.7.3.0 (likely), main up to dcff1cf
fixed_in: 0.7.3.1
branch: fix/w7-legless
commits: 52369bf 7325b15 02bcf2e (tests ac3f711 f126847)
tests: tools/tests/coop_legless_guest.py (legless_guest, legless_guest_replace, _done, _cancel, _dontsave)
signature:
docs: docs/re/creations.md#creations-own-editor-save, docs/06-testing.md#testing-legless-guest, docs/known-issues.md (BUG-0178 item)
related: BUG-0178
---

## Report
A player on Discord (2026-10-29, two friends who had just bought the game, version not given, most
likely 0.7.3.0): "we can't really get through phase 2; I spawn with the phase 1 cell without legs;
the host still can play normally; can't change my character".

Known so far (docs/known-issues.md, BUG-0178 item; docs/re/creations.md at the start of the Creature
stage): a guest without own creations gets its own editor on the host's creature as it was before
the host's editor (the legless one made from the cell). Saving that edit makes the pre-legs creature
the guest's own (kept in choices.ini) while the host plays the legged one; nothing offers a way to
change it later. Whether this is what the player hit, or the guest never got the host's evolved
species at all, is open.

## Root cause
Observed on lane 8 (GOG 2024, `tools/tests/coop_legless_guest.py`, docs/re/creations.md#creations-own-editor-save):

* The guest's own editor (opened by the first Creature vote on the creature the guest plays: the
  host's legless creature made from the cell) is the "edit a creation" editor. After an edit, "done"
  asks Save New / Replace / Don't Save. The saved creation goes into the guest's My Creations, but
  the avatar keeps the world's species (the host's legless pre-editor key).
* Replace (run 20261029-150227): the return from the editor (`adopt_after_editor`) saw an avatar key
  that was not the newest own creation and took the avatar's key, the host's legless 290D79C6, as the
  guest's own creature (`choices.ini`; log "back from the editor, own creature 290D79C6").
  `apply_own_avatar` then put it on every avatar the host's worlds brought (the host's evolved
  290D79CA included), after a rejoin too; the host showed the guest with that key, its own old
  legless creature. Both screens: legless.
* Save New (run 20261029-150227): the new creation kept the key it was made from, the host's
  290D780E. As the guest's newest own creation it was its own; on the guest that key answers with the
  host's package (priority 2000 above My Creations), so the guest played the legless creature on its
  own screen while the host showed the guest's legged records under a remapped key ("the host plays
  normally").
* "Can't change my character": the co-op window's arrows only cycle the guest's own My Creations
  (no way back to the world's species), `choices.ini` keeps the pin across sessions, and a player who
  does not know the nest's mating call has no other editor.
* Done without a change and Don't Save keep the world's species: the guest plays the host's evolved
  creature after the reload (run 20261029-143204, done). Not the cause.

Inferred: the player's guest picked Save New or Replace in that editor (the only paths that give the
legless creature), most likely 0.7.3.0 (the same code as main here).

## Fix
mods/coop/creations.cpp, switch `creations.pre_editor` (`coop.fix creations.pre_editor off` = 0.7.3.0):
* back from an editor a guest takes over only the newest own creation that was not in My Creations
  when the editor opened (read again every 2 s for 30 s); nothing of the world's is pinned (the host
  keeps the old way);
* a pinned key of the host's world that is not one of ours (old `choices.ini`) is ignored;
* an own creation under a key of the host's world is used under its shared remapped key (the
  records the host shows);
* the co-op window's arrows include "the world's own" (the host's current creature, sent by key).

Not changed: the guest's own editor is still the "edit a creation" editor (BUG-0283 notes another
oddity of it).

## Verification
Lane 8, GOG 2024, `tools/tests/coop_legless_guest.py`:
* FAIL with `SPORE_FIX_OFF=creations.pre_editor`: out/lab/20261029-154403 (legless_guest: the guest
  plays the host's pre-editor 290D81CC on both screens, after a rejoin too, the co-op window has no
  way back; legless_guest_replace: the same with 290D8408). Before the fix (dcff1cf):
  out/lab/20261029-150227 (new, replace).
* PASS with the fix: out/lab/20261029-160318 (legless_guest, legless_guest_replace,
  legless_guest_done: all checks); out/lab/20261029-153321 (new, replace: every check of the fix
  passed, the next-editor check failed on a test bug fixed in f126847).
* Exploration runs: out/lab/20261029-143204 (done, legs without the dialog, cancel without Quit).

## History
- 2026-10-29: opened.
- 2026-10-29: fix/w7-legless: reproduced (Save New, Replace), fixed behind creations.pre_editor; BUG-0283 opened for the own editor showing the host's new parts.
- 2026-10-29: released in 0.7.3.1.
