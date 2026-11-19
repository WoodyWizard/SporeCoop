---
id: BUG-1271
title: WP-T1: Tribe members of a guest are of the guest's species on every game (saves keep the host's)
status: fixed
kind: feature
stage: Tribe
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in: unreleased
branch: feat/w54-tribe, feat/w54-shims
commits: d6ffc32d e7dcf746 696d51e2 48dd3e02 0db36ea8
tests: tribe_own_species
signature:
docs: docs/re/tribe.md#tribe-own-species, docs/06-testing.md#testing-tribe-own-species
related: BUG-1270, BUG-1272, BUG-1156, BUG-1154, BUG-1173, BUG-0318
---

## Report
Plan item (docs/plan-0.7.16.md item 1, WP-T1): in Tribe every member was the host world's species (the tribe's slot);
the owner wants each player's members to be of that player's own creature on every game, the host's save keeping the
host's species only (owner decisions 2026-11-18).

## Root cause
A new member takes the tribe's species slot (Citizen_Init 0xC27330); a member's species is its own field (+0xB20 /
+0xB28) that nothing of the game puts back and a save keeps per member (probe tribe_species_probe, lab
20261118-132622, docs/re/tribe.md#tribe-two-species). Nothing gave a guest's members the guest's species.

## Fix
mods/coop/tribe_species.cpp (docs/re/tribe.md#tribe-own-species): every game, a key compare every 0.15 s; a member
owned by a player with an own line gets that species (SetSpecies keepState 1 + the outfitter's tool / hand animation
steps), one per pass, not under a cinematic, not in a world's first 3 s, not at the end of Tribe; a member the mod
changed goes back to the slot's when its owner has no line (gift to the host, a leave, creature.mode host, the switch
off, the session end). Save hooks write the slot's key into the changed members' +0xB28 while a save is written.
Rule `tribe_logic::species_act` (unit tests in tests/tribe_tests.cpp). The line is a WP-0 shim (`// WP-0 shim`:
creations::player_key(pid, kCreature)) until species_line (BUG-1270) is merged. Switch `coop.fix tribe.own_species`.

## Verification
`make test` passes (tribe_tests species_act). Lab runs pending (owner's rule 2026-11-18: no lab runs until the night
run): `tribe_own_species` PASS with the fix, FAIL with `SPORE_FIX_OFF=tribe.own_species`; regressions tribe_outfit,
tribe_guest_baby, tribe_portraits, tribe_owners_session, tribe_mirror_hunger, tribe_species_probe.
* Lab (fix/w57-savestrip, lane 4): PASS lab 20261119-114816 and, on rel/0.7.16 with BUG-1313's kid rule (the test's
  kid checks follow it), lab 20261119-133307 tribe_own_species (every check, 0 faults; the save check
  needed BUG-1315, the carry check BUG-1317, a fault after the reload BUG-1320 = BUG-1313). FAIL twin
  `SPORE_FIX_OFF=tribe.own_species`: lab 20261119-101851 (step 2: every member the slot's species on both games).

## History
- 2026-11-18: opened.
- 2026-11-18: implemented on feat/w54-tribe (code, test, docs); lab runs pending.
- 2026-11-18: feat/w54-shims: the WP-0 shim `line_key` is species::key(owner, Stage::Tribe), on_change starts the next pass, the save strip also takes members showing a line key (species::is_line_key); the test sets a tribe outfit with `coop.species test tribe next` and checks the 0x7B key, its return from the store after a new evening, and the outfit's instance in the save. Still open: the night run.
- 2026-11-19: fix/w57-savestrip: the night run's failures were the save (BUG-1315), the test's carry read-out (BUG-1317) and a fault of SetSpecies on a young member (BUG-1320, a duplicate of BUG-1313 fixed on fix/w57-tribe-keys); PASS / FAIL runs above.
