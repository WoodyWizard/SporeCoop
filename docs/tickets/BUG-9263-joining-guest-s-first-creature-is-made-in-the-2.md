---
id: BUG-9263
title: Joining guest's first creature is made in the 2,000 DNA Creature Creator with every part unlocked
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: owner 2026-11-19
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in: 0.7.14.1-lineages66-dev
branch: feat/lineages-47
commits: 1d8737b6
tests: tools/qa_win/qa.py (manual: MSI host Samuth, a fresh guest profile joins; with the switch off and on)
signature: a joining guest's first editor shows "$2,000" and every part (Creature Creator, Test Drive tip)
docs:
related: BUG-9207, BUG-9208, BUG-9259, BUG-9164
---

## Report
Owner 2026-11-18: "When BigTexas was hosting, It let me create a new creature with 2,000 DNA and all items unlocked".
2026-11-19: "We need the unlocked Creature Creator to be fixed. We can't let them make a level 5 endgame creature upon
joining any player." With BUG-9259's first-creature fix the guest keeps that creature in the world.

## Root cause
Observed (MSI 2026-11-19, `coop.fix creature.join_start_editor off`): a fresh guest joining a Creature world got
the Creature Creator (0x465C50BA, "$2,000", all parts, the "Test Drive" tip). coop.cpp pregame_editor_tick (BUG-9207 /
BUG-9208) submitted `submit_new_creature_request()`, whose default editor is the Creator, whenever the host's start
editor parts list (3Z B) was not there: always for a late joiner of an existing world, and for a host in the Creator.

## Fix
Commit 1d8737b6: the first-creature editor is always the Creature stage's start editor (0xE46C381E: 145 DNA; tabs legs and
hands, the second holding the three diet mouths, cell-era parts and eyes), with the host's start list when there is
one (3Z B). A host in the Creator gives its guests no editor (the late path follows once the host is in its world).
Switch: `coop.fix creature.join_start_editor off` (the Creator as before). Note: the start editor's mouths are the top
row of its second tab; "Mouthless" is only the warning of a body with no mouth yet (docs/re/creature.md#creature-stage-start-editor).

## Verification
no-fail-run: two PCs, the game's editor. Manual 2026-11-19 on the MSI pair (host Samuth, a fresh guest profile each
run): FAIL with `coop.fix creature.join_start_editor off`: the Creature Creator, "$2,000", every part. PASS with it on:
the start editor, "$145"; a herbivore mouth placed (130 left), named, the check saved it; the held world loaded and the
guest played its own creature ("plays its own creature 'Naurkawne'"), its family followed its species (1 member); it
was killed by an NPC at the nest (the host is killed at the same spot when this save loads) and came back at its own
nest with 40/40 and a family member beside it.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on feat/lineages-47 (1d8737b6).
