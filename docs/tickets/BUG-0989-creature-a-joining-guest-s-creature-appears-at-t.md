---
id: BUG-0989
title: Creature: a joining guest's creature appears at the host's creature's exact spot (drawn inside each other, other poses on each game)
status: open
kind: bug
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 (key-frame review)
role: 
versions: 0.7.13-rc
fixed_in:
branch:
commits:
tests: same_view, play_creature, find_layout (key frames)
signature:
docs:
related: BUG-0340
---

## Report
Key-frame review of the rel/0.7.13 final check (lab out/lab/20261112-175447): on the creature fixture both
games show the host's and the guest's creatures at one point, drawn inside each other, from the first frame on
(041 play_creature start, 031 find_layout, 034 menu_splash, 050/053 same_view: both avatars at
(-171.8 -467.8 -84.0) in the manifests). In 050 and 053 the two bodies have other poses on the two games (on the
host Bob stands upright on Alice / lies over her, on the guest Alice stands upright in front of him).

## Root cause
Inferred, not analysed: the guest loads the host's save, so its avatar starts where the save has the player's
creature, which is where the host's creature stands; the host's puppet of the guest is put at the guest's
position. Nothing moves one of them aside. The poses differ because each game resolves two interpenetrating
bodies (its own avatar's physics and IK, the puppet's relay) on its own. A player who joins sees this until one
of them walks. Related: BUG-0340 (Tribe chiefs at one spot, wontfix).

## Fix
Not done (for later): put a joining guest's avatar a few metres beside the host's creature on the ground
(the game's surface, as `coop.npc gopos` does since BUG-0964), or the host's puppet beside its own avatar.

## Verification
no-fail-run: open, not fixed (seen in 5 key frames of one run)

## History
- 2026-11-12: opened.
- 2026-11-12: re-check key frames (out/lab/20261112-225741, sheet 013): at the guest's join ('loading the world of Alice') the guest's tag 'Alice' hangs over the guest's own creature for a moment, Alice's creature stands to the left; right on the next frame (014). Likely the same spawn point cause.
