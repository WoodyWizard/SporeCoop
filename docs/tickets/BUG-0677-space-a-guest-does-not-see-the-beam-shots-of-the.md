---
id: BUG-0677
title: Space: a guest does not see the beam shots of the ships the host's game runs (no default_beam_projectile in a guest's game)
status: fixed
kind: desync
stage: Space
reported: 2026-11-07
source: lab log sweep BUG-0657 (detector Space:noun_only_host / host_only:default_beam_projectile)
role: 
versions: 0.7.8, 0.7.9, rel/0.7.10
fixed_in: unreleased
branch: fix/w46-k56-space
commits: 5eace570, d14618ed
tests: space_shots
signature:
docs: docs/re/space.md#space-ship-shots, docs/06-testing.md#testing-space-ship-shots, docs/known-issues.md#issue-space-beams
related: BUG-0477, BUG-0657
---

## Report
Detector (lab log sweep, BUG-0657): `Space:noun_only_host:default_beam_projectile` (16 runs) and `Space:host_only:default_beam_projectile`
(12 runs, e.g. play_space, campaign_space; "1 default_beam_projectile near the guest on the host, none on the guest"). Unlike the Civ
shots this class persists: the next sample still had it in 11 of 14 / 8 of 11 cases, and no run ever had a beam in the guest's game
only or unmatched (the guest's game has none at all). How to see it: play_space with SPORE_DETECTOR=20, then
`tools/detector_report.py <run>`.

## Root cause
Inferred from the code (not run): squadron.cpp h_FireTool returns 0 for every mirror ship in a guest's game ("mirrors never fire on
their own"), the hits come from the host (MSG_UFO_HIT); civ::fire_observed relays the host's FireTool calls only for Civ vehicles and
turrets (BUG-0477: "The Space half (`default_beam_projectile`) is not covered here"). So the guest sees the host's ships and the NPC
ships the host runs fight without their beams. Not fixed in 0.7.10 S1 (a relay of Space shots like civ.shots is a feature-size
change; found by the S1 sweep, outside its six tickets).
Observed (lab 20261108-021047, the fix off): on a planet the host's pirates fired in 78 samples of 45 s (147 projectile
objects in the host's noun list), the guest's game had none in any sample: the mirrors' FireTool is refused by the
mod's hook, and no shot of the host's ships was relayed (only Civ shots, civ.shots). In a star system the spawned
pirates made no FireTool call at all (lab 20261108-020128): NPC ships fight on planets.

## Fix
`space.shots` (squadron.cpp; the civ.shots pattern): the host counts the shots its listed NPC ships made (FireTool
true: weapon slot +0x790..+0x7A4, aim, target: a listed ship, a player's ship, a noun near a place) and sends the last
second's list in MSG_UFO_STATE with chunk 0xFE / chunks 0 (an older guest drops it); the guest fires its mirror's weapon
of the same slot that many times at the same target with the game's FireTool, never at a target it lacks (BUG-0534),
and stops the weapon when the ship leaves the list. Every hit of a mirror in the guest's game is dropped (squadron
TakeDamage, civ.cpp cosmetic_hit for buildings in Space); the host's MSG_UFO_DAMAGE still brings the damage.
`coop.fix space.shots off`: mirrors never fire (0.7.10).

## Verification
coop_space_shots.py (space_shots, lane 9): FAIL with SPORE_FIX_OFF=space.shots, lab 20261108-021047 (guest projectiles
in 0 of the host's 78 samples). PASS lab 20261108-020511 (76 of 77 samples, 139 / 150 projectiles, 761 of 770 shots
fired, 760 copied hits dropped, no fault) and lab 20261108-021358.

## History
- 2026-11-07: opened.
- 2026-11-08 (fix/w46-k56-space): fixed with space.shots; test space_shots FAIL/PASS.
