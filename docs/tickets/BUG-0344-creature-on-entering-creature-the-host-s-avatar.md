---
id: BUG-0344
title: Creature: on entering Creature the host's avatar becomes an old own creature from choices.ini, and its species looks unevolved after the editor
status: fixed
kind: bug
stage: Creature
reported: 2026-10-31
source: player report 2026-10-31 (host mark)
role: host
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w22-reports
commits: 2a4420a
tests: host_old_choice (tools/tests/coop_host_old_choice.py)
signature:
docs: docs/re/creations.md
related: BUG-0341, BUG-0278, BUG-0345
switch: creations.host_world_species
---

## Report
Host (GOG 2024, Windows, 0.7.3.1) with one guest: "After evolving into creature stage my own
species are still unevolved" (in Creature, about 2.5 minutes after the stage change, after the
first creature editor of the stage).

## Root cause
Observed (host log):
* `Cell -> Creature`: the game gives the avatar the species the host just made (29104762); the
  co-op trace at once says `own=29081E2F '<old creature>' from choices.ini, ... the game has
  29104762 (DIFFERENT)`, and 3 s later `creations: own avatar is now 29081E2F` (SetSpecies): the
  host's avatar becomes an own creature the host chose in an earlier session (another world)
  instead of the creature it just evolved.
* The world is saved for the guest 1 s after that swap, so the shared save names the My Creations
  creature (see BUG-0341, the guest crashed loading exactly this save).
* After the creature editor the game has 2910476F (the edited species), the trace keeps saying
  `own=29081E2F ... (DIFFERENT)` for the rest of the session: the edit is not taken over as the
  host's own (inferred from the code: `adopt_after_editor` compares
  against the avatar's key on the first frame back, which still was 29081E2F, and stops).

Inferred: for the host the world's species is its own; a choices.ini choice from another world
should not replace a species the host has just made in this world. "Still unevolved" is the host
seeing the old creature (its avatar or the puppet the guest is shown) instead of the new species.

## Fix
`creations.host_world_species` (on; off = 0.7.3.1): on the host a creature or cell choice holds only
in the stage world it was made in (`game::stage_epoch` of Creature / Cell at `choose`); a choice from
choices.ini of an earlier session, or one made before the stage changed (Cell -> Creature), is
ignored: the host plays its world's species. Back from an editor the host's choice is dropped, so the
editor's result (the avatar's species) is its own. A choice made now, in this world, still swaps.

## Verification
`tools/tests/coop_host_old_choice.py` (suite `host_old_choice`), host choices.ini preset to Tiger
Horse (twoinst.PRESET_CHOICES):
* FAIL with `SPORE_FIX_OFF=creations.host_world_species`: out/w22/old_choice_fail2.txt (host avatar
  290229B9, the guest draws the host as 7CF18B79).
* PASS: out/w22/old_choice_pass2.txt (both 065CBE70; the in-world choice still swaps).
Lab: FAIL 20261031-125112 (fix off), PASS 20261031-125210. Not tested: the editor return (adopt) part.

## History
- 2026-10-31: opened from the host's report zip (mark, creations and trace lines).
- 2026-10-31: fixed on fix/w22-reports (creations.host_world_species); the second batch (fix/w22-reports2 evidence) adds: every load re-swapped the host.
- 2026-11-01: released in 0.7.3.2.
