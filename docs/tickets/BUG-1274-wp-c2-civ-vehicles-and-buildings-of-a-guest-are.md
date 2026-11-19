---
id: BUG-1274
title: WP-C2: Civ vehicles and buildings of a guest are of the guest's designs (saves keep the host's)
status: open
kind: feature
stage: Civ
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in:
branch: feat/w54-designs
commits:
tests: civ_own_designs, civ_own_designs_off
signature:
docs: docs/re/civ.md#civ-own-designs, docs/01-architecture.md#arch-species-line-packages
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

## Verification
tools/tests/coop_civ_own_designs.py (`civ_own_designs`; twin `civ_own_designs_off` = SPORE_FIX_OFF=civ.own_designs,
must FAIL). Lab runs pending: the owner stopped every lab run on 2026-11-18 until the night run (no run dirs yet).

## History
- 2026-11-18: opened.
- 2026-11-18: code, test and twin on feat/w54-designs (built, make test); lab runs wait for the night run.
- 2026-11-18: feat/w54-shims: one path for designs: species::design / species::set_design (MSG_SPECIES_LINE line creations, 0x7B keys); MSG_CREATION kind 9 (share_design, player_design) removed before any release; `coop.civ owndesign set` goes through species::set_design; the save strip also gives an object showing a line key without a record the culture set's key (designs::culture_key); on_change sweeps at once. Still open: the night run (civ_own_designs, twin civ_own_designs_off).
