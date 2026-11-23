---
id: BUG-1313
title: tribe: SetSpecies on a tribe kid (a baby) faults in the game's growth timer (both games)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-19
source: lab
role: 
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w57-tribe-keys
commits: 4ce6f9de
tests: tribe_kid_species, tribe_kid_species_off (coop_own_editors_robust.py kid)
signature: 0xC0000005 EIP 0x40A00000 / 0x00000000, calls 00BC326E 00C03A94 (Timer_Start < cCreatureAnimal::SetSpecies+0x174)
docs: docs/re/tribe.md#tribe-kid-setspecies
related: BUG-1271, BUG-1299
---

## Report
Lab 20261119-105127, tribe_birth_species_off (the BUG-1299 twin): both games faulted in the same frame when the
species pass changed the guest's new baby 0.5 s after it was seen (guest EIP 0x40A00000, host EIP 0; the game's
handler caught it, frames went on).

## Root cause
✔ (Ghidra GOG 2024, docs/re/tribe.md#tribe-kid-setspecies) cCreatureAnimal::SetSpecies 0xC03920 starts the growth
timer at creature +0x1628 (Timer_Start 0xBC3260, `call [this+0x1C]`) for a creature that is not grown up (+0xB34 != 1)
in the Creature or Tribe mode. A tribe member is a citizen with no timer there. Any kid the pass changed faulted: a kid
whose owner became known after its birth (the host's baby paired later, an unclaimed baby), a kid whose owner's line
changed or went away (revert), and every baby with tribe.birth_species off.

## Fix
`tribe.kid_species`: the pass (tribe_species.cpp) leaves a kid as it is until it is grown up (counter waits_kid in
`coop.tribe species`); a kid of a known owner is born in its species anyway (tribe.birth_species). `coop.tribe
setspecies <key> <id>` never touches a kid. Off: a kid is changed like an adult (the fault).

## Verification
FAIL (twin, SPORE_FIX_OFF=tribe.kid_species): tribe_kid_species_off 20261119-113824 (the kid changed to the new key on
both games, the host faulted: EIP 0xFFFFFFFF, calls 00BC326E 00C03A94); before the fix: tribe_birth_species_off
20261119-105127 (both games). PASS: tribe_kid_species 20261119-113824, merged tree 20261119-121054 (the kid keeps
7CBC3253, the adults 7C382858, no fault); tribe_birth_species_off 20261119-113824 no fault any more.

## History
- 2026-11-19: opened (fix/w57-tribe-keys, from the night run's twin).
- 2026-11-19: fixed on fix/w57-tribe-keys.
- 2026-11-23: released in 0.7.16.
