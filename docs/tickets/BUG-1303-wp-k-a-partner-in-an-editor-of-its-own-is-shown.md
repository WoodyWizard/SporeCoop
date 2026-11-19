---
id: BUG-1303
title: WP-K: a partner in an editor of its own is shown in the game's cocoon where it stood and comes out with its new body (it disappeared before)
status: open
kind: bug
stage: Creature
reported: 2026-11-19
source: plan
role: any
versions: 0.7.15.2 and older
fixed_in:
branch: feat/w56-cocoon
commits:
tests: cocoon, cocoon_guest, cocoon_off
signature:
docs: docs/re/creature.md#creature-cocoon, docs/06-testing.md#testing-cocoon
related: BUG-0516, BUG-0923, BUG-0451, BUG-1304, BUG-1305
---

## Report
Plan 0.7.16, WP-K (owner approval 2026-11-19): in the Creature stage, while a player is in an editor of its own (the
host's mating editor, a guest's "Evolve now" editor or its later editor at the nest), every other game should show
that player's creature wrapping into the game's own cocoon and holding in it at that spot; back, the creature stays in
the cocoon until it wears the new design, then comes out. The Lineages fork (PR #196, its BUG-9160 / 9181 / 9194) did
this.

## Root cause
Observed (0.7.15.2, rel/0.7.15.2 lab run 20261118-144132 editor_now, the guest's log): the partner's state in its
editor is mode Editor, the same world crc, flags 0; drive_puppet hides a partner that is not in the world, so its
creature vanished ("Alice hidden: flags=0x0 ... same_place=0") and a new puppet appeared when it was back ~18 s later
("Alice appeared in the world"). (The plan's text "the puppet just stands and then swaps its body" was not what the
lab shows: it disappeared.)

## Fix
`mods/coop/cocoon.cpp` (cocoon.h), wired in puppets.cpp drive_puppet / despawn / forget_puppets / on_remote_anim and
game.cpp (game::hold_pose: the idle refresh is skipped for a held body). A partner whose state says it is in an editor
in this world keeps its puppet, which plays the game's cocoon (docs/re/creature.md#creature-cocoon); back, it stays in
the cocoon until it wears the partner's new creature (a new body goes into the same cocoon), then emerges. No new
message: a 0.7.15.2 partner is shown in the cocoon too, a 0.7.15.2 game hides the partner as before. Effects released
with the puppet and only in the mode / world they were made in. Switch `coop.fix creature.cocoon off`: hidden as
before.

## Verification
Lab not run yet (the orchestrator's "lab ok" is pending): `cocoon` and `cocoon_guest` must PASS, the twin `cocoon_off`
must FAIL (step 1: no puppet while the partner edits).

## History
- 2026-11-19: opened.
- 2026-11-19: implemented on feat/w56-cocoon (build, make test); lab pending.
