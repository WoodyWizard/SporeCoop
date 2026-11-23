---
id: BUG-1324
title: test: a guest's single click on its hut sometimes buys nothing under lab load (tribe_guest_baby, tribe_kid_species)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-19
source: lab
role: 
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w57-tribe-keys
commits: faf0d043
tests: tribe_guest_baby, tribe_birth_species, tribe_kid_species (coop_tribe_guest_baby.buy_click)
signature: tribe_guest_baby: "the host's game has the member the guest bought (4 -> 4)", baby_pair seen=0 on both games, food 60 -> 60
docs:
related: BUG-0671, BUG-1299, BUG-1313
---

## Report
The night run's regression run 20261119-102828 (6 lanes): tribe_guest_baby FAILED with no member, no food paid on either
game and no hut effect seen on either game (`baby_pair seen=0`); also tribe_kid_species in 20261119-115724 (no new
member). The orchestrator suspected BUG-1299's tribe.birth_species hook on cTribe_SpeciesSlot.

## Root cause
✔ Not the birth hook: the purchase never happened in the guest's own game (no hut effect 0xDB6F2233, its food stayed),
so BabyBorn (where birth_begin runs, 5 s after the effect, and only on the host: the guest sends CMD_BABY) never came;
the slot hook answers the original slot outside birth_begin. Not reproduced in 4 runs of the same order on lane 5
(20261119-110853, -111028, -111324 with tribe_civ, creature, tribe_coop before it as on lane 6, and 20261119-115724).
~ inferred: the single hover + click (0.6 s) was not taken by the game under load (fps 20, frames of 5-7 s in those
runs) or hit a member walking in front of the hut; coop_tribe_outfit.open_outfitter already clicks up to three times
for the same reason ("the first click after a while may only take the focus").

## Fix
`buy_click` (coop_tribe_guest_baby.py): hover, 20 frames, click; again (at most 3) only when the guest's game took none
(no food paid and no new hut effect after 2.5 s). A purchase pays and starts its effect at once, so a second click
never buys twice; the checks (one member, food down by the price once) are unchanged. The test also prints both
tribes' level / cap and the windows under the cursor before the click.

## Verification
no-fail-run: not reproducible on demand (load). PASS with the helper: tribe_guest_baby 20261119-121054,
tribe_kid_species 20261119-121054 (one click), tribe_birth_species 20261119-121054.

## History
- 2026-11-19: opened and fixed on fix/w57-tribe-keys.
- 2026-11-23: released in 0.7.16.
