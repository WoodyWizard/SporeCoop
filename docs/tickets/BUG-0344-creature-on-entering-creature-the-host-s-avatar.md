---
id: BUG-0344
title: Creature: on entering Creature the host's avatar becomes an old own creature from choices.ini, and its species looks unevolved after the editor
status: open
kind: bug
stage: Creature
reported: 2026-10-31
source: player report 2026-10-31 (host mark)
role: host
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creations.md
related: BUG-0341, BUG-0278, BUG-0345
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
Not done. Proposal (`coop.fix creations.host_world_species`): on the host a choices.ini creature
choice is used only in the world (save) it was chosen in, and an evolution (Cell -> Creature, an
editor) always makes the new species the host's own; the guest's own creations stay as they are
(BUG-0345 for the design question).

## Verification
Test to write: the host has an own creature chosen (choices.ini of the test profile), plays
Cell -> Creature (fixture cell world, `coop_campaign.py --from cell --to creature`); then the
host's avatar species is the species the stage editor made (game state), on both machines.

## History
- 2026-10-31: opened from the host's report zip (mark, creations and trace lines).
