---
id: BUG-0937
title: Creature: the guest's HUD portrait keeps the first species after its own creature changed or after the host's evolution in creature.mode host
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: guest
versions: 0.7.12 (unreleased)
fixed_in: 0.7.13
branch: fix/w48-labels
commits: bcaf0792 a98f2cb7 79858616
tests: hud_portrait
signature:
docs: docs/re/creature.md#creature-posed-top-portrait
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
Found 2026-11-12 (fix/w48-labels, Ghidra + lane 4): the portrait window 0x03D99151 is set only by the Creature HUD
refresh 0xD2C000 (`UTFWin::Image::SetBackgroundByKey` with the png {avatar +0xB20 -> +0x504, 0x2F7D0004, +0x50C}).
The mod's body swaps (`cCreatureBase::SetSpecies` on the avatar) change the avatar's species key (lab: 290225CD ->
290217AC) and never run that refresh.

## Fix
Fixed 2026-11-12 (fix/w48-labels), switch `creature.hud_portrait` (hud_portrait.cpp): in Creature, twice a second outside
the frame, when the window's image (its ImageDrawable +0x24) is not the game's image of the avatar's species png, the
game's SetBackgroundByKey sets it. Off: as before (0.7.12).

## Verification
`hud_portrait` (`coop_hud_portrait.py`, creature): the guest changes its creature (`coop.creations next creature`);
within 8 s the window's image is the png of the new species (`coop.portrait` same=1) and the portrait in a shot
changed (mean difference over 12 of 255; 56 with the fix, 0 without).
* FAIL (`SPORE_FIX_OFF=creature.hud_portrait`): out/lab/20261112-123755.
* PASS: out/lab/20261112-122824, out/lab/20261112-123359-2.
* The creature.mode host case (the host's evolved creature) goes through the same check; one_creature runs in the
  final check of this branch, not with a portrait assertion.

## History
- 2026-11-11: opened.
- 2026-11-12: fixed on fix/w48-labels (creature.hud_portrait), hud_portrait FAIL 20261112-123755, PASS 20261112-122824 and 20261112-123359-2.
- 2026-11-12: released in 0.7.13.
