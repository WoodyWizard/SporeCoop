---
id: BUG-0937
title: Creature: the guest's HUD portrait keeps the first species after its own creature changed or after the host's evolution in creature.mode host
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0920, BUG-0928, BUG-0923
---

## Report
Key-frame review of the second final check out/lab/20261111-090547 (rel/0.7.12 94ca59ab).
Sheets: `out/lab/20261111-090547/frames/sheets/001_appeared_once_01_relook.jpg`, `out/lab/20261111-090547/frames/sheets/029_one_creature_02_evolved.jpg`.
Frames: `out/lab/20261111-090547/frames/appeared_once/01_relook_guest.jpg`, `out/lab/20261111-090547/frames/one_creature/02_evolved_host.jpg`, `02_evolved_guest.jpg`
(the portrait is the square at the bottom right of the Creature HUD, under the health and hunger bars).

* appeared_once: the guest changed its creature to a tall grey and purple one; both games draw it so (the host
  under the tag "Bob", the guest in front of its own camera). The guest's HUD portrait still shows the red
  creature of the host's species it started with.
* one_creature `evolved` (creature.mode host, after the host's editor): the host's portrait shows the new creature
  (small, with horns); the guest is drawn as the new creature on both screens but its portrait still shows the old
  red one.
* The host's own portrait is right in both.

Severity: low (cosmetic: the player's own picture in the HUD is stale). Confidence: high for these two frames.
Regression of 0.7.12: probably not (the portrait is the game's; before the BUG-0920 fix the guest's screen was
black at this frame, so it could not be seen in 20261111-044257).

## Root cause
Not analysed. Inferred (medium): the mod swaps the model of the guest's avatar (own creature, or the host's new
one) without changing what the game takes the portrait from (the player's species thumbnail, made when the stage
was entered), and nothing asks the HUD to draw it again. The Tribe roster's pictures had a similar source
(BUG-0921: the creation bundle's PNG).

## Fix
Open.

## Verification
Open. How to test: appeared_once and one_creature: after the swap compare the HUD portrait of the guest with the
one of a game that plays that creature from the start (a golden image of the portrait window, or the image key
the window holds read by `coop.ui`). FAIL today on both.

## History
- 2026-11-11: opened.
