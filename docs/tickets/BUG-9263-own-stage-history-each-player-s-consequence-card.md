---
id: BUG-9263
title: Own stage history: each player's consequence cards and the powers they give
status: open
kind: feature
stage: All
reported: 2026-11-20
source: owner
role: 
versions: 
fixed_in:
branch: feat/own-cards
commits:
tests: tools/tests/coop_own_cards.py
signature:
docs: docs/re/creations.md#creations-own-cards, docs/01-architecture.md#architecture-own-cards, docs/04-netcode.md#netcode-own-cards, docs/known-issues.md#issue-own-cards-limits
related: BUG-9264, BUG-9260, BUG-9258
---

## Report
The owner's friend: every player should have their own stage history (the card each finished stage gives: Cell
carnivore / herbivore / omnivore, Creature, Tribe, Civ, the Space archetype) and the consequence powers that follow
from their own cards; one player's choices never change the other's. Every game showed the host's.

## Root cause
Every game has one cPlayer (cards at +0x10FC..+0x110C, Creature progress +0x1278 / +0x127C), loaded from the host's
save, and the wsync row "stages" kept writing the host's cards into every guest's game. A stage end runs only in the
host's game (the guests get its world). Where each stage decides its card, the ids, the readers:
docs/re/creations.md#creations-own-cards (objdump of the in-memory Steam .text + runtime reads and calls, lane C).

## Fix
mods/coop/cards.{h,cpp}: each party seat keeps its own cards and progress (party file PTY2, reads PTY1); when the
host's world has a stage done that a seat has not, the host decides the seat's card (Cell: the guest's own game's
decision; Creature: the game's decision with the seat's progress; Tribe / Civ: the host's, one shared tribe /
nation; Space: the game's archetype of the seat's four cards) and sends each guest its record (MSG_OWN_CARDS 75,
session 1.3); the guest's game writes it into its cPlayer, the "stages" row leaves it alone. Switch
`coop.fix party.own_cards off`: one shared history (0.7.15). Limits: docs/known-issues.md#issue-own-cards-limits.

## Verification
By hand on this PC, lane C (Steam build on Windows, instances 5 / 6, the owner's Creature save AMOR), the steps of
tools/tests/coop_own_cards.py driven over the consoles: with the fix PASS (the guest's game shows its own Creature card
17E5EF84 while the host's shows FD159D91, also after a rejoin and after the host's reload of the planet; the game's
own getter cPlayer::GetConsequenceTrait gives each game's own); `coop.fix party.own_cards off` FAILS at 3 (the guest's
game shows the host's FD159D91). Not yet run in the Linux lab.

## History
- 2026-11-20: opened.
