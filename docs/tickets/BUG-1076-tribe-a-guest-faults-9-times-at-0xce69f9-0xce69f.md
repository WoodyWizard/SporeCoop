---
id: BUG-1076
title: Tribe: a guest faults 9 times at 0xCE69F9/0xCE69FB (dc04aee5, data 0x3F400000 / 0x7C) while the host's players' hut falls in the campaign's Tribe end
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-fault
commits: 3a96a1f6
tests: tribe_own_hut_fall
signature: SporeApp.exe+0x8E69F9 / +0x8E69FB (0xCE69F9 / 0xCE69FB) read 0x3F400000 / 0x7C, caught
docs: docs/re/tribe.md#tribe-caption-tribe-token, docs/06-testing.md#testing-own-hut-fall
related: BUG-0672, BUG-0777, BUG-0960, BUG-0983, BUG-1081
---

## Report
fix/w49-tests full campaign, lab 20261113-082316 (lane 4), guest Dave (instance 34, 4 players, the campaign's own
planet Cadian): during the Tribe end's raids (09:18-09:24) Dave's game caught 9 faults, the first at 09:18:55
`0xC0000005 at 0x00CE69F9 SporeApp.exe+0x8E69F9 (data 0x3F400000)`, then 0x00CE69FB (data 0x7C), tid 772 (main),
called from (stack scan) App::Property::SetValueFloat+0x185D < FNV_hash+0x126 < GlobalUI_FindWindowByID+0x28E4.
Next lines in Dave's log: the guest ended the cinematic 94D17D9F (TRG_Upgrade, BUG-0983), a dead member 12 m over the
ground, "a hut falls here ... the hut at (155 -468 -104) fell in the host's game" (the players' own hut) and the
conquest of the village at (-4 -506 4). The campaign's `fault` findings caught it (the run fails).
Log: SPORE-lab/lane4 sporekit.34.log of that run (copied by the lab into out/lab/20261113-082316 when it ends).

## Root cause
Observed (Ghidra GOG 2024, the guest's log of lab 20261113-082316):
* 0xCE69F9 / 0xCE69FB lie in `SporeCoop::TribeTokens_Resolve` 0xCE67E0, vt 0x10 of the Tribe stage's text token
  provider (`TribeHud_ptr` 0x169B41C, vtable 0x14779D0; the BUG-0645 "HUD object" was this provider: CORRECTION in
  the .sym and docs/re/tribe.md#tribe-caption-tribe-token). Token 0xF7A4AD93 (eax in both faults) is the tribe's name:
  `[provider+0x1C]` vt 0x7C, a cTribe* kept without a reference, checked for null only. ecx = 0x3F400000 (the freed
  tribe's first dword reused), then edx = 0 (vtable gone). The stack scan's names are stale words; the frames are a
  caption's LocalizedString being built. Not the species / planet record code the first note guessed.
* +0x1C is set by `TribeVillage_Defeated` 0xCDAC90 (a conquest: the village's tribe) and by the relation events of
  0xCDF080; the conquest captions (TRG_TotemPole_Conquer, TRG_Upgrade) read it while they play.
* The guest's log: 09:17:57 "realign: the hut at (155 -468 -104) fell in the host's game: it falls here" was the
  PLAYERS' OWN hut (an NPC village raided it back in the host's game; the campaign repaired it 19 s later). The guest's
  game ran its own defeat (busy cinematic,input_lock, then paused) and 20 s later reloaded its own save (`Tribe ->
  Loading -> Tribe`, epoch 2). After each reload the realign conquered the village (-4 -506 4) and felled the own hut
  again: 11 reloads by 09:24 (epochs 2..13). The conquest just before a reload set +0x1C; the captions after the
  reload read the tribe the reload had freed: the faults 3-4 s into TRG_Upgrade (09:18:55, 09:19:34, ...).
* Mod cause: realign.cpp's BUG-0777 hut fall bound the guest's own hut (fl player) to the host's dead own hut and
  called tribe::fell_hut on it; nothing excluded the player tribe (conquer_village does).
Inferred: the freed-tribe read needs the guest's own reload; the host's own game over has the same +0x1C hazard in
principle (a conquest just before its hut falls), not seen.

## Fix
realign.cpp: a guest's own players' hut (local fl kFlPlayer) dead in the host's game never takes the realign's blow;
the health sync below keeps it at the host's health and 1 hp at least. The host's game decides the defeat (its game
over and reload, then the realign of the new world). Switch `coop.fix realign.own_hut` (off: 0.7.13, the own hut
falls). `coop.realign` prints `own_hut=` and `own_hut_kept=`. Test command `coop.tribe hutfall <x> <y> <z>` (host /
local play): the game's killing blow on a hut.

## Verification
Test tribe_own_hut_fall (docs/06-testing.md#testing-own-hut-fall): the host fells an NPC village's hut and the players'
own hut at once; the guest's game must keep its own hut (state never 2, same object) for 45 s, no fault.
* FAIL with `SPORE_LAB_FIXOFF="tribe_own_hut_fall=realign.own_hut"`: lab 20261113-132309 (lane 6): the guest's hut
  fell 3 s after the host's, key frame: the guest's own "Oh, the humanity! Your tribal hut has been destroyed!"
  caption.
* PASS: lab 20261113-132309-2 (lane 7), 20261113-132632 (lane 6), 20261113-134749 (lane 6, cold, with the BUG-1075 start guard): the hut stands (st 3/1000, fl 1), `own_hut_kept`
  23.
* Regression: realign_tribe, tribe_conquest_guest (20261113-132632), play_tribe --quick, tribe_away_conquest
  (20261113-132632-2).
The 0xCE69F9 faults themselves were not reproduced in the twin (they need the guest's own reload with a conquest
caption after it; the FAIL run's guest stood in its defeat caption for 45 s without reloading).

## History
- 2026-11-13: opened.
- 2026-11-13: analysed and fixed on fix/w49-fault (realign.own_hut); BUG-1081 opened for the host's hut caption.
