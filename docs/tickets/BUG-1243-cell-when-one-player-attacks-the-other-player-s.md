---
id: BUG-1243
title: Cell: when one player attacks, the other player's cell plays the attack animation too
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video
role: all
versions: <0.7.15
fixed_in:
branch: fix/w52-evoeditor
commits:
tests: cell_attack_anim, cell_attack_electric, cell_electric
signature:
docs: docs/known-issues.md#issue-partner-cell-attack-anim, docs/re/cell.md#cell-partner-attack-anim
related: BUG-1241, BUG-0715, BUG-0716
---

## Report
A players' video (players on a version before 0.7.15), seen by the owner: in the Cell stage, when one player
attacks something, the other player's cell plays the attack animation too.

## Root cause
Not found. Measured with `coop.pool anims` (the cell animation +0x1B0 of the own cell and of every partner's puppet,
both games, every 0.05 s) and `coop.effects` (cell fixture, both cells the world's species):
* Each player in turn bites the nearest creature (the game's Bite, after swimming at it): no animation of the
  attacker reached the other player's own cell or its puppet (out/lab/20261118-095915, -100729); the fixture's cell has
  no weapon, its bite plays no attacker animation (+0x1B0 stays 0 on the attacker too).
* The host's cell given the electric part (`coop.pool borrow electric graft`: the game zaps by itself, animation 0x1E
  on the attacker), both cells next to the same creature: the host's avatar played 0x1E, the guest's own cell never,
  the host's puppet of the guest never; the game's electric step 0xE71520 was never called for a puppet cell
  (`puppet_zaps 0`, out/lab/20261118-102558, -102601, -102744), although the puppet shares the host's model data.
  (The puppet's electric step was the suspected shared cause with BUG-1241, a puppet running its own attack; a hold of
  puppet zaps was written and taken out again for lack of a case.)
* The cell puppets get no animation relay at all (MSG_ANIM is dropped for cells, BUG-0796); what shows a partner's
  attack is the effects relay of world effects near the attacker (8 sent / 8 played per bite round).
Not tried: a jaw or spike part (no fixture cell has one), two cells overlapping each other (the partner's relayed
effects drawn where both cells are), poison clouds of a partner (BUG-0716 makes them in the host's game on purpose).

## Fix
None: not reproduced.

## Verification
no-fail-run: not reproduced on v0.7.15 (runs above).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w52-evoeditor): not reproduced (bites, the electric part); known-issues entry.
