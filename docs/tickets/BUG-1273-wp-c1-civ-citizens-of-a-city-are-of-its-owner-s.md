---
id: BUG-1273
title: WP-C1: Civ citizens of a city are of its owner's species
status: fixed
kind: feature
stage: Civ
reported: 2026-11-18
source: plan-0.7.16
role: both
versions: rel/0.7.16
fixed_in: unreleased
branch: feat/w54-citizens, feat/w54-shims
commits: 86059027 696d51e2 48dd3e02 04f44839
tests: civ_own_species (coop_civ_own_species.py), civ_own_species_hostmode (--host-mode)
signature: the guest's city's citizens are of the host's (the civilization's) species on both games
docs: docs/re/civ.md#civ-own-citizens, docs/re/civ.md#civ-own-species, docs/06-testing.md#testing-civ-own-species
related: BUG-1270, BUG-1274, BUG-1221, BUG-0476
switch: civ.own_species, civ.own_species_save
---

## Report
Plan 0.7.16, item 2 (WP-C1): in the normal co-op every player has its own species; in Civilization every citizen of
every city of the players was the host's (the civilization's) species. Owner decision 2026-11-18: a city's citizens are
of the species of the player who owns it; the shared capital's of the player who founded or captured it (it changes with
the owner); the host's save holds the host's species only.

## Root cause
Observed (Ghidra, docs/re/civ.md#civ-own-species): CityCitizens_Sync 0xD5BF50 gives every pool citizen the species
City_Species 0xBD9F90 answers (the civilization's key +0x268) and puts it back on each run.
Observed (runtime, lab 20261118-180539 lane 6, the first version of the hook): hooking City_Species changes the citizens'
species on both games (15 of 19 citizens of the guest's captured city became the guest's creature key 7CBC3253 on the
host and on the guest; the capital kept 19 of the civilization's 067C7743). Two gaps seen in that run:
* 4 of the 19 stayed the civilization's: the citizen manager's four special citizens (mgr +0xA78 + i*0x14, made once per
  city visit by 0xD5C6D0) are not walked by the sync;
* the host's save (`coop.save` with the camera at the guest's city) had the guest's key in its citizen records (type
  0x018EB4B7): city citizens are saved with their species key.

## Fix
mods/coop/civ_species.cpp (switch `coop.fix civ.own_species`):
* City_Species is hooked: a city whose owner (g_city_owner, known on every game) is a guest with an own species line
  answers that line's species profile; the host's cities, the shared capital (its founder: the host), NPC cities and
  `creature.mode` host answer the game's own (civlogic::citizen_line_owner, unit-tested). The line is a `// WP-0 shim`:
  the guest's own creature (creations::player_key(pid, kCreature), only when it is not the host's) until
  species::key(pid, Stage::Civ) of WP-0 (BUG-1270) is merged.
* Every 0.5 s, outside the frame, the citizen manager's city is checked: a pool citizen or one of the 4 special ones of
  another species than the city's answer -> CityCitizens_Sync(mgr, 0) once (at most every 3 s per city) and
  CreatureBase_SetSpeciesProfile 0xC22450 for the special ones. So an owner change (capture, gift, the cities dealt
  anew), a line that arrives later and the switch going off are all applied without a message.
* Saves (`coop.fix civ.own_species_save`): while a save is written every citizen with a guest line's key gets the
  player civilization's key in +0xB28..+0xB30 (no rebuild) and its own back at the end (savestrip save hooks).
Nothing is sent, nothing is written into the civilization.

## Verification
To run (owner's no-lab rule from 2026-11-18 evening; lane 6):
* `civ_own_species`: PASS expected; `SPORE_FIX_OFF=civ.own_species civ_own_species`: FAIL (step 3);
  `SPORE_FIX_OFF=civ.own_species_save civ_own_species`: FAIL (step 4, as seen in lab 20261118-180539 before the strip);
  `civ_own_species_hostmode`: PASS.
* Regressions: civ_citizens, civ_citizens_reload, civ_designs.
Seen so far: lab 20261118-180539 (the hook without the special citizens and the strip): FAIL on exactly those two
checks, the capital and the hook's effect as expected.
* Lab (fix/w57-savestrip, lane 4): PASS lab 20261119-114816 and 20261119-123654 (merged tree) civ_own_species (also 20261119-113320, 20261119-112418);
  FAIL twins: `civ.own_species` off lab 20261119-101851 (step 3), `savestrip.keys` off lab 20261119-120449 (the save
  check: the citizens' strip `save_stripped=38` did not keep the key out, BUG-1315); the night run's save failures
  (20261119-095414, 20261119-102655 with civ.own_species_save off) were BUG-1315. civ_own_species_hostmode PASS in
  20261119-095414. Not rerun here: civ_citizens, civ_citizens_reload, civ_designs. Seen in the key frames: BUG-1321.

## History
- 2026-11-18: opened.
- 2026-11-18: feat/w54-citizens: runtime check of the hook (lab 20261118-180539), the design, the test; the PASS / FAIL
  twin runs wait for the night run.
- 2026-11-18: feat/w54-shims: the WP-0 shim `line_key` is species::key(owner, Stage::Civ), species::own_mode, on_change; civ.own_species_save also strips citizens that show any line key (species::is_line_key); the test sets a Civ outfit with `coop.species test civ next` and checks the 0x7B key on both games. Still open: the night run.
- 2026-11-19: fix/w57-savestrip: the save check fixed by BUG-1315 (the citizens' strip stays as a guard, with counters save_starts / save_seen / save_nociv); fixed.
