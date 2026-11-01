---
id: BUG-0357
title: space: a share save posted in the solar system was written after the host landed on a planet: the guest that loaded it faults every frame at 0xEBDD12 (and 0xFD9420)
status: fixed
kind: bug
stage: space
reported: 2026-10-31
source: lab 20261031-185843 campaign_space#3 (lane 7)
role: guest
versions: main 55da78b
fixed_in: 0.7.3.2
branch: fix/w27-space-fault
commits: 3f1c37d 9532fa0 19896b3
tests: space_share_landing (coop_space_share_landing.py), campaign_space
signature: dc04aee5:0x00EBDD12 (data 0x8, every frame, calls 00F47269 00BA855C 00C0B858 ... 01017D3A 00FF63F5) with a few dc04aee5:0x00FD9420 (data 0x1C); host log "saving for the share of player N (Space, ...)" then "stage: Space 80xxxxxx -> Space 40xxxxxx" before "save: N objects of ours are kept out of it"
docs: docs/re/space.md#space-share-save-on-planet
related: BUG-0301, BUG-0276, BUG-0287
---

## Report
Final verification lab 20261031-185843 (main 55da78b), campaign_space#3 (lane 7, 4 players, Civ ->
Space, cold start). After its rejoin in Space the third player's game caught 4476 faults in two
minutes (findings 441, +1855, +1016, +1164; campaign folder out/lane7/campaign/20261031-195112):
4465 at 0x00EBDD12 (data 0x8) and 11 at 0x00FD9420 (data 0x1C), both on the main thread; its console
did not answer for 10 s at a time and it had no frames for 15 s. Its ship sat on the home planet
with the host's ship values (hp 8/1500) while the host was in the solar system. No crash.

## Root cause
Observed (lab logs): the host posted the share save for the rejoining guest at 21:04:20.128 in its
solar system (place 0x80AE3044, "14.5 s in this place"); 0.29 s later its ship was on a planet
(0x40AE3044, the test bot had sent it there); the game serialized the world at 21:04:21.226 (1.1 s
after the post, the machine was heavily loaded), so on the planet. `save_game` checks the Space
context only when it posts the save message; the game writes the save frames later. The guest
loaded that planet-made save: the documented failure of a Space save made on a planet
(docs/07-modes.md: "such a save loads with UI errors every frame (0xEBDD12)").
Observed (Ghidra, GOG 2024 and 2016 alike): 0xEBDD12 is FUN_00EBDBB0+0x162 (thiscall, ret 8), an
area-effect / damage pass whose `this` is null (`mov eax,[esp+0x18]; mov edi,[eax+8]`); 0xFD9420 is
cCityData::GetSpiceProduction with a null city data (from 0xC82B70). The symbol labels in the log
("SporeApp.exe+0xABDD12", "Movie::MovieSystem::Init+0x350", "Herd_Update") are only nearest names.
Reproduced (lab 20261031-204700, 20261031-204721): the host's ship lands 0.3 s / 0.7 s after the
share save is posted (`coop.worldfix landsave <ms>`, the game's LandOnActivePlanet): the save is
written on the planet and the guest faults ~1000 times in 15 s at 0xEBDD12 (same calls list as the
lab) and at 0xFD9420, its ship on the planet. A landing in the frame of the post instead makes the
game drop the save (nothing written; BUG-0276's check saves again): lab 20261031-204503.
Inferred: what exactly is missing after such a load (a planet hazard manager, the cities of a
tech-5 planet record) is not traced further.
Pre-existing: the race is as old as the Space share save check (v0.5.0, commit 3989b34); 1 of 182
Space share saves in the lab history hit it. Not caused by a recent change; BUG-0301's settle clock
only covers the moment of the post.

## Fix
savestrip's COMSerializer::Write hook calls a watch when a save starts writing the world (once per
serializer and frame, main thread, also with the strip off); world.cpp notes when that happens in
Space with `Simulator::GetCurrentContext` == 0 (planet). A share whose save was written on a planet
is saved again ("world: the save for player N was written on a planet ...: saving again off the
planet"); `save_game` then waits until the ship leaves the planet ("a player is waiting for the
world: leave the planet to save"). Switch `coop.fix world.share_planet_save` (off: the share goes
out with that save, 0.7.3.1). Test knob `coop.worldfix landsave <ms>|-1`.

## Verification
- FAIL (SPORE_FIX_OFF=world.share_planet_save): lab 20261031-204700 space_share_landing (lane 9,
  landing 0.3 s after the post) and 20261031-204721 (lane 10, 0.7 s): guest faults 1152 / 1169 at
  0x00EBDD12 plus 0x00FD9420, guest place 40460035 (planet), host 80460035.
- PASS: lab 20261031-205039 space_share_landing #1 (lane 9) and #2 (lane 10): "the save for player 1 was
  written on a planet ... saving again off the planet", the second save after the takeoff, guest
  faults 0, guest in the host's system 80460035.
- Related: lab 20261031-205237 space_share_takeoff + space_share_landing (lane 9) PASS; lab
  20261031-205302 (lane 10) and 20261031-205600 (lane 9) campaign_space PASS (4 bots, Civ -> Space);
  make test passes.

## History
- 2026-10-31: opened (fix/w27-space-fault) from lab 20261031-185843 campaign_space#3; reproduced with
  a 2-player test, fixed.
- 2026-11-01: released in 0.7.3.2.
