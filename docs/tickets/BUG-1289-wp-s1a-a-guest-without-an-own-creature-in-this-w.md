---
id: BUG-1289
title: WP-S1a: a guest without an own creature in this world plays the host's species in Tribe and Civ and cannot pick its own
status: fixed
kind: bug
stage: Tribe, Civ
reported: 2026-11-19
source: plan
role: guest
versions: 0.7.15.2, rel/0.7.16
fixed_in: 0.7.16
branch: feat/w56-late
commits: c29e3ba4 4b9fe0de 4ce6f9de
tests: late_species, late_species_civ, late_species_off and late_species_civ_off (twins)
signature:
docs: docs/re/creations.md#creations-late-species, docs/06-testing.md#testing-late-joiners
related: BUG-0345, BUG-0638, BUG-1270, BUG-1271, BUG-1273, BUG-1290
switch: creations.late_species
---

## Report
Plan item WP-S1a (out/research/lineages-species.md, ranked item 1, from the Lineages fork's late joiners): a guest
without an own creature in this world (it joined at Tribe or Civ, the host loaded a save of a planet the guest never
played, or it chose its creature in the galaxy menu) plays the host's species in Tribe and Civ and cannot pick another.
Its species line has no creature, so its tribe members and city citizens stay the host's on every game.

## Root cause
Observed in code (no runtime yet): the co-op window's creation row (coop.cpp `a.creation`) existed only in Cell,
Creature and Space; BUG-0638 (creations.cpp choice_here) drops an explicit choice that has no worlds.ini record for
the current world, and `choose()` recorded no world when it was made outside one (world crc 0) or recorded the planet
played before (the galaxy menu keeps that name), so a choice made before the join was dropped in the host's world.

## Fix
`creations.late_species` (creations.cpp, coop.cpp, coop_ui.cpp): in Tribe and Civ the creature row shows for a guest
that has own creatures and plays none of them here (not after its own "the world's own" choice) and for 30 s after a
click; selected units take the row back for the give button; in Civ the arrows pick the creature, not the role, while
the row shows it. The choice is recorded for this world like every in-world choice. A choice made outside a world (no
world name or the galaxy menu) holds for the first Cell .. Civ world of the session and is recorded there (a session
state); a choices.ini entry of an earlier run is still dropped (BUG-0638's purpose). The row also shows in the galaxy
menu for a connected guest. Once per world, 15 s after the arrival in Tribe or Civ, such a guest is told how to pick.
The species line follows by itself (own_creature), so its members and citizens become its creature on every game.
Off: as 0.7.15.

## Verification
Night run 20261119-104136: late_species FAILED at step 3 (BUG-1312: the host never sent the guest's emptied row) and
step 4 (the galaxy menu's choice 290217AC lost: `choose` ran before anything had read the profile's paths, and
find_paths' first read loaded choices.ini's old 290225CD over it; a player's own clicks go through refresh_options,
which reads the paths first, so this was the test's direct command; choose() now reads the paths first, 4ce6f9de).
PASS: late_species 20261119-112300, merged tree 20261119-115724; late_species_civ PASS 20261119-104136.
FAIL (twin, SPORE_FIX_OFF=creations.late_species): late_species_off 20261119-112300 (no row in Tribe, the menu's choice
dropped: the right reason); late_species_civ_off FAIL 20261119-105127.

## History
- 2026-11-19: opened.
- 2026-11-19: code, tests and twins on feat/w56-late; lab pending.
- 2026-11-19: fix/w57-tribe-keys: night run causes found (BUG-1312, choose() order), lab PASS and twin FAIL; fixed.
- 2026-11-23: released in 0.7.16.
