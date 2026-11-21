---
id: BUG-9293
title: Own start at any stage: in a new game started at Creature, Tribe, Civ or Space every guest chooses its own creature and the cards of the earlier stages
status: open
kind: feature
stage: Creature, Tribe, Civ, Space
reported: 2026-11-20
source: owner 2026-11-20
role: host, guest
versions: 0.7.15.95
fixed_in:
branch: feat/start-choice-any
commits:
tests: start_any_tribe, start_any_civ, start_any_off
signature:
docs: docs/01-architecture.md#architecture-start-any, docs/re/creations.md#creations-start-any
related: BUG-9273, BUG-9263, BUG-9260, BUG-9262, BUG-9268, BUG-9274
---

## Report
The owner (2026-11-20): the partner's own choice must not be only in Cell: whatever stage the host starts a NEW game in
(Cell, Creature, Tribe, Civ, Space from the galaxy menu), the partner gets its own choice too: its own creature (from
its My Creations), the path / consequence traits (the cards: Cell diet, Creature social / aggressive, Tribe, Civ) and
what the game asks a player starting at that stage.

## Root cause
A feature. Before, only the Cell start asked a guest (BUG-9273); in a new game at a later stage a guest's party seat
started from the host's cards and the guest played the world's species (or the creature it had picked in F9).

## Fix
start_any (docs/01-architecture.md#architecture-start-any): MSG_START_ANY 110 (session 1.7); the host asks a seated
guest in a new game's Creature / Tribe / Civ / Space world (party::planet_new); the answer gives the guest's game its
chosen creature (creations::choose: its party seat, nest, village and nation follow) and the guest's seat its chosen
cards of every earlier stage as its own history (party::set_history; cards.cpp keeps them for stages the host's world
has not done). `coop.fix party.start_any off`. RE: docs/re/creations.md#creations-start-any.

## Verification
Lane A on Windows (Steam build, instances 1/2), new games from the galaxy menu (driver
E:\Repositorio\SporeCoop-wt\agentA5\anyt.py, ngame.py; screenshots in that folder):
* Tribe (Cerenhet): asked; the guest picked 292846CB, Cell carnivore, Creature social: the seat CFB01B93 FD159D91,
  the guest's cPlayer the same, the host's 0; seat species 7CA81684 (= the guest's creature), the guest's own village
  founded with it (any_tribe_guest_own_village.png): PASS.
* Creature (Golypetia): Cell carnivore in the seat and the guest's History; the guest plays its own creature next to
  the host's Angel Pods (any_creature_guest.png): PASS.
* Civ (Demos): Cell, Creature, Tribe cards (CFB01B93 FD159D91 C2CA9495) in the seat and the guest's cPlayer; seat
  species 7CA81684: PASS.
* Twin (Tretak, a new Tribe game with `coop.fix party.start_any off` on the host): not asked, the seat's cards 0:
  FAIL as expected.
Not checked: the Space start (stage 4), the guest's own nation in that Civ game (the session setting of own nations
was off), the Linux lab run of start_any_tribe / start_any_civ / start_any_off.

## History
- 2026-11-21: opened and implemented on feat/start-choice-any, tested on lane A.
- 2026-11-21: the co-op window's "Your own start" page (the creature by name with < >, one card row per earlier stage,
  Start): answered by clicking on lane A (Tribe, Civ, Space: PASS). Civ with civ.own_nations on: the guest's own nation
  00002411 (seat 1) is of its chosen creature 7CA81684, allied with the host's: PASS. Merged feat/lineages-space-launch
  (a620e40f); Space start (the host takes off for the share): the guest's seat and History have its four cards and the
  archetype the game computes from them (F966D429 knight; the host's is A5656E84), its own empire (seat 1) has the
  chosen creature as captain (7CA81684): PASS. Two fixes on the way: a guest of the host's Space save is "here" for the
  question (every player's place is its own star), and creations::choose records a choice by the same world rule
  mine() reads it with (record_world: the host's world while the game has no avatar, as in Space; it was recorded
  under the star and never held there).
