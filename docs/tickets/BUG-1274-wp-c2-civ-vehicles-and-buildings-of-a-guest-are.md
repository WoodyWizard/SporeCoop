---
id: BUG-1274
title: WP-C2: Civ vehicles and buildings of a guest are of the guest's designs (saves keep the host's)
status: fixed
kind: feature
stage: Civ
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in: unreleased
branch: feat/w54-designs, feat/w54-shims, fix/w57-civlook
commits: 696d51e2 48dd3e02 0452cf09 2fc331e7 b29a5c6c
tests: civ_own_designs, civ_own_designs_off, line_stages, line_stages_nodesign_off
signature:
docs: docs/re/civ.md#civ-own-designs, docs/re/civ.md#civ-vehicle-no-design, docs/06-testing.md#testing-civ-look, docs/01-architecture.md#arch-species-line-packages
related: BUG-1270, BUG-0647, BUG-0670, BUG-1157, BUG-1158
---

## Report
Plan item 2 (docs/plan-0.7.16.md, WP-C2): in Civ every vehicle and building was the culture set's design (the host's),
also those of a guest's cities and a guest's vehicles. Owner decisions 2026-11-18: a gifted vehicle takes the
receiver's design; the host's save keeps only the host's designs.

## Root cause
One culture set per civilization (cCivilization +0x6C); the game draws every vehicle and building from a model key of
its own (+0x34 +0x90 = +0xC4), SetModelKey changes it with the design's stats, Civ_SetDesign re-models a whole kind
(Ghidra, docs/re/civ.md#civ-own-species; per-object SetModelKey seen working in BUG-0647 / BUG-0670 / BUG-1157).

## Fix
`civ.own_designs` (mods/coop/civ_own_designs.cpp, docs/re/civ.md#civ-own-designs): every game re-keys the vehicles
and buildings owned by a player with an own design for their design id (four times a second, after a purchase, at the
end of a hooked Civ_SetDesign), keeps their culture key and writes it into +0xC4 while a save is written (savestrip
hooks), puts it back for a gift to a player without a design, creature.mode host, the switch off and the session end.
The guest's designs are its species line's (species::design, BUG-1270: one line creation per design, host-relayed on
MSG_SPECIES_LINE, the same 0x7B...... key on every game; the lab's setter `coop.civ owndesign set <id> <creation>`
calls species::set_design). (Before feat/w54-shims: creations::share_design, MSG_CREATION kind 9, never released.)
`coop.fix civ.own_designs off`: everything the culture's design (0.7.15).

fix/w57-civlook (the night run's failures, 2026-11-19):
* Root cause 1 (line_stages, lab 20261119-133307: "+0xC4 0 on both games"): a Civ game started from Tribe has a land
  vehicle design only for the capital's specialty; the guest's vehicle bought in its captured city of another
  specialty was made with the zero key {0,0,0} (no model, 10 hit points), and the re-key skipped every object with
  +0xC4 0 ("no model yet"). Fix `civ.own_designs_nodesign`: such a vehicle of an owner with an own design gets it; its
  culture key is the zero key (written back raw for a save, SetModelKey with it at the end as cVehicle::Load did).
* Root cause 2 (civ_own_designs, not seen in the night run because of the harness traceback before it): with every
  object stripped, the host's save still named the guest's design keys: the vehicle design in an event of the main
  stream 0x01897C18 and the cPlayer record 0x02C21781, the house design in the house record at +0x224 (a second key
  field). Fix: a savestrip key filter (BUG-1315's mechanism, now also for the vehicle and building model types in the
  dword blocks and event lists) writes the culture set's key of the design id the line names it for.
* The test harness traceback (struct.error on an empty DBPF index in keys_in, night run 20261119-095414): the same
  catch as fix/w57-own-editors (BUG-1307); civ_own_designs prints the bytes around a key of ours found in a save.

## Verification
* civ_own_designs (SPORE_FIX_OFF=civ.own_designs twin `civ_own_designs_off`): FAIL lab 20261119-142443
  civ_own_designs_off (the guest's vehicle and houses the culture's designs); PASS lab 20261119-142443 and, on the tree
  merged with rel/0.7.16 (741c292b), lab 20261119-143657-2 and after the rel/0.7.16 merge of 15:14 (a42100f2) lab 20261119-151431. Before the save filter: FAIL 20261119-140235 /
  20261119-141949 (the save named 7BE222FB in the main stream).
* line_stages (vehicle step, twin `line_stages_nodesign_off` = SPORE_FIX_OFF=civ.own_designs_nodesign): FAIL lab
  20261119-143657 (+0xC4 00000000 on both games, as the night's 20261119-133307 of fix/w57-savestrip); PASS lab
  20261119-142443-2 (the guest's vehicle 7BE222FB built from 76023C9D on both games, hp 368.2, the host's Civ save
  clean) and on the merged tree lab 20261119-143657.
* fix/w58-linestages (the tree with WP-X1 / the own editors merged, c4132601): the guest's vehicle shows its design on
  both games in all three line tests: line_stages lab 20261119-184827 (`owndesign set`, the host's culture key 0),
  line_stages_outfit 20261119-190903 / 20261119-182732 (the same path, 7BE222FB built from 76023C9D; the guest's culture
  key a generated 0x29...... design, re-keyed), line_stages_editors 20261119-190903 (its own vehicle editor's design
  7B67193D built from 7AF016DB, the culture's 3A5843B7). Twin line_stages_nodesign_off FAIL lab 20261119-173854 (+0xC4
  0, max hp 10). The "built from none" of fix/w57-own-editors 20261119-162318 (line_stages_outfit) did not come back
  in 4 runs of that path; line_stages now prints the vehicle's whole model key (type, group) when the check fails.

## History
- 2026-11-18: opened.
- 2026-11-18: code, test and twin on feat/w54-designs (built, make test); lab runs wait for the night run.
- 2026-11-18: feat/w54-shims: one path for designs: species::design / species::set_design (MSG_SPECIES_LINE line creations, 0x7B keys); MSG_CREATION kind 9 (share_design, player_design) removed before any release; `coop.civ owndesign set` goes through species::set_design; the save strip also gives an object showing a line key without a record the culture set's key (designs::culture_key); on_change sweeps at once. Still open: the night run (civ_own_designs, twin civ_own_designs_off).
- 2026-11-19: fix/w57-civlook: the zero culture key (civ.own_designs_nodesign) and the save's key filter for design keys; the harness catch; fixed with the runs above.
