---
id: BUG-9206
title: Creature: a player's hatchling size is not handled: tiny until it grows on its own game, adult x1.15 on partners' screens
status: fixed
kind: bug
stage: Creature
reported: 2026-11-14
source: owner
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: a8a3ceca
tests: tools/qa_win (TITAN, host 9 + guest 8 over 127.0.0.1, 2026-11-14)
signature:
docs: docs/re/creature.md#creature-party-nests-hatchling-size
related: BUG-0014 BUG-9181 BUG-9194
---

## Report
Owner play test 2026-11-14 (screenshots): after evolving, a guest looked larger than the host on the host's screen; and a player coming out of the egg is so small it is practically invisible until it grows.

## Root cause
Inferred from logs and code (medium-high confidence), one root: a player's hatchling state (size 0.1, age 0) is not handled for players. (a) Own game: a respawn or hatch sets the avatar's scale to 0.1 and a family member's greeting normally sets the real size (docs/re/creature.md 13); a guest's family are synced copies with no AI, so nobody greets it and it stays at 0.1 until `hatch_tick` (npc.cpp around 1878) steps in, which waited 23 s after the editor return (guest log 18:56:54 "the hatchling waited for its family ... it grows up now"). (b) Partners' screens: a puppet is spawned adult and made 15 % bigger (modes.cpp around 152, `set_scale(p, game::scale(p) * 1.15f)`); a player's own size is never sent outside Scenario (`captain_scale`, modes.cpp around 85 and 481), and `puppet_back` (evolve.cpp around 242) gives the new body the old body's size. Fix idea: send the own size in Creature too (the captain_scale field), apply it to the puppet, and set the normal hatchling size at once when hatch_tick sees a waiting hatchling.

## Fix
modes.cpp: in Creature each player sends its own size (the Adventure captain's field, BUG-9113); a partner's body is spawned at it and kept at it outside the cocoon (coop.cpp drive_puppet), no longer an adult made 15 % bigger. npc.cpp hatch_tick: a guest's waiting hatchling gets the game's hatchling size at once (the creature's vt 0x80(0), as the family's greeting does) and grows up after 1 s instead of 4 s. Switch: `coop.fix creature.size_sync off` (the hatchling size and wait are not switched).

## Verification
no-fail-run: manual on TITAN. FAIL (19:43, the build before): the guest's body on the host had radius 2.14 (the host's own 1.32); after the editor the guest's hatchling stayed at 0.1 (an empty nest in the egg scene) until it grew 15 s later. PASS (20:03, a8a3ceca): the guest's own radius 2.04 and its body on the host 2.04; through a hatch, sampled every 0.5 s: own scale 0.10 in the egg, 0.587 (radius 1.04) the moment it waited, the host's view 1.04, then grown 1.73 own / 1.71 on the host.

## History
- 2026-11-14: opened.
- 2026-11-14 (night): fixed (decision 3V A), live on TITAN.
