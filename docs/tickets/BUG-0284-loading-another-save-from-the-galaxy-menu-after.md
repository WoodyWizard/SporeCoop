---
id: BUG-0284
title: Loading another save from the galaxy menu after a stage left with a fault: crash in cStarRecord::GetPlanetRecord
status: fixed
kind: crash
stage: Tribe/Civ
reported: 2026-10-29
source: lab campaign 20261029-174717 (4 players, lane 7)
role: host
versions: 0.7.3.0 (main b5ef74d)
fixed_in: unreleased
branch: fix/w8-planet
commits: 48324e9 302a898
tests: coop_menu_stale_player.py (suite menu_stale_player)
signature: 0xC0000005 at 0x00BBABE2 Simulator::cStarRecord::GetPlanetRecord+0x12 read of 0 (eax=ecx=0) < cStarManager::GetPlanetRecord+0x3B < cEmpire_RequireHomePlanet+0x65 < GetPlayerHomePlanetRecord+0x15 < 0xBB4E8A (stars.db write 0xBB4D40, labelled RequirePlanetsForStar+0x1FA), mode GalaxyGameEntry
docs: docs/re/space.md#space-menu-player-reset, docs/06-testing.md#testing-menu-stale-player, docs/known-issues.md#issue-tribe-leave-hears-fault
related: BUG-0285
---

## Report
The final 4-player campaign (lab run 20261029-174717, lane 7, GOG 2024, host, main b5ef74d): the
tribe did not end in 20 min, so the host jumped to the civ_npc fixture in game (`coop.load`, slot
10). The host left Tribe for the galaxy menu; in the frame the mode changed the game caught a fault
of its own (BUG-0285, CreatureHearsTarget+0x463 in a citizen's WalkTo). 3.0 s after the menu came
up the mod started the load ("world: loading from the galaxy menu"); 0.7 s later, before
"world: loading Tavael.spo" was logged (so inside the mod's load_now), the game crashed:
0xC0000005 at 0x00BBABE2 `cStarRecord::GetPlanetRecord+0x12`, read of 0. The coop.dll frames of
the stack scan are stale (net code); the scan stops inside the 5 KB frame of 0xBB4D40, so the
entries after 0xBB4E8A (ResetRelationship+0x1D9, ...) are stale too. The same jump passed 7 times
that day; of ~100 logged leaves to the menu before a load, this was the only one with a fault.

## Root cause
Observed (disassembly + probes + memory reads on lane 5, docs/re/space.md#space-menu-player-reset):
* The game's reset for the galaxy menu (0xB32C30) clears the player record (SpacePlayerData_Reset
  0x1022350: empire id -1, cached cEmpire* released) before it reads the stars. In the menu the id
  is -1 and GetPlayerEmpire returns 0.
* The mod's `load_now` (world.cpp) calls `ReadStarsFromSaveDB` for the new slot. That calls
  0xBB61C0, which (RNG-dependent; it did in every lane-5 run) writes stars.db through 0xBB4D40,
  which ends with `GetPlayerHomePlanetRecord`. With the player empty that returns at once.
* With the old world's empire still there, `RequireHomePlanet` looks up its home planet id
  (0x00C9C01E in the tribe fixture) in the NEW slot's star table: star 0xC9C01E of civ_npc has no
  planets (count 0, records null) and `cStarRecord::GetPlanetRecord` reads records[0]: the crash.
* Patching out the reset's call (0xB32CF0) during the leave reproduces the crash with the same
  address, registers (eax=ecx=0, esp=0012C040) and stack (lane 5, scratch run 2026-10-29 20:09).
Inferred: the handled fault BUG-0285, in the frame the mode changed, unwound past 0xB32CF0, so the
menu kept the old empire. Not proven which frame held the game's handler.
The mod is involved: the game's own path never reads another slot's stars with a player set; the
mod's direct ReadStarsFromSaveDB call (BUG-0157-era load from the menu) is what met the stale
player. A stale cached cEmpire* would also be returned by GetPlayerEmpire after the load when the
new save's player has the same id (both fixtures use 800021CA).

## Fix
`load_now` (mods/coop/world.cpp): in the galaxy menu, when `GetPlayerEmpireID` is not -1, call the
game's own `SpacePlayerData_Reset` (what the menu reset would have done) before reading the new
slot's stars; a warning line in the log, a counter in `coop.status` ("stale menu player cleared
N"). Switch: `coop.fix world.menu_player off` = the old behaviour. GOG 2016 (frozen): the symbol is
missing there, so the fix is off (data/symbols/frozen_off_922af7c7.txt).
The fault itself (BUG-0285) stays open.

## Verification
`coop_menu_stale_player.py` (suite `menu_stale_player`): the tribe fixture, 3 rounds of
tribe -> civ_npc -> tribe loads from the galaxy menu with the menu's player reset patched out
(the stand-in for the game fault, which cannot be made on purpose).
* FAIL without the fix (`SPORE_FIX_OFF=world.menu_player`, lane 5): lab run
  20261029-191412-fix-w8-planet: the host crashed in round 1 at 0x00BBABE2
  `cStarRecord::GetPlanetRecord+0x12`, the campaign's address, registers and stack.
* PASS with the fix: lab run 20261029-191921-fix-w8-planet (2 runs, lanes 6 and 8): 6 loads each,
  "stale menu player cleared" 6 of 6, no fault on either game.
Not verified: the original trigger (the game fault BUG-0285) was not reproduced; that it skipped the
player reset is inferred from the one campaign crash.

## History
- 2026-10-29: opened; root cause, fix and test on fix/w8-planet.
