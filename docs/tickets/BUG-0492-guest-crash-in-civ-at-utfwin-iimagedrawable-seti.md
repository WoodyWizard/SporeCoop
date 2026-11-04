---
id: BUG-0492
title: guest crash in Civ at UTFWin::IImageDrawable::SetImageForWindow+0x21F (null +0xC) under cPlanetRecord::CalculateDeltaSpiceProduction, 0.7.3.6
status: fixed
kind: crash
stage: Civ
reported: 2026-11-03
source: player report SporeCoop-report-20261103-194448.zip (owner downloaded 2026-11-03; 0.7.3.6, Steam guest, Civ), out/reports/2026-11-03
role: guest
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-civ-crashes
commits: df52d3ed, 9a9262ab
tests: civ_discovery
signature: 3010a413:SporeApp.exe UTFWin::IImageDrawable::SetImageForWindow+0x21F
docs: docs/re/civ.md#civ-planner-lifetime
related: BUG-0491, BUG-0346
---

## Report
Guest (Steam), Civ, 19:29:30, 529 s into the session, after the host founded 9 cities in 30 s (civ: the host founded a city ... cities now 9): 0xC0000005 at 0x13AF7AF (data 0xC), chain SetImageForWindow+0x1778 < +0x17BA < cPlanetRecord::CalculateDeltaSpiceProduction+0x268C (frame names misleading, BUG-0346). A UI image (city / spice panel) of an object the guest's game does not have in full (a founded mirror city?) is null.
Target: 0.7.4 (first: these are crashes on the current release).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (disassembly): 0xE2F7AF is in 0xE2F6B0, the filling of a "UI/AssetDiscoveryCard" made by
  0xE30CF0 (cUIAssetDiscoveryManager), which reads `data+0xC` with no null check; the caller chain
  0xC7472C / 0xCE8DBA / 0xCF66EF is "meet a civilization" 0xC746F0 called by the city click handler
  0xCE8C30 (a first click on a city hall: its political id). The card data 0xBA5B60 is null when
  GetCivilizationByPoliticalID(pol) is null.
* Observed (lab): `coop.civ discover 7EADBEEF` (the game's meet function with a political id no
  civilization has) crashes the guest at 0xE2F7AF read 0xC, the report's signature.
* Inferred: the guest clicked a city whose owner its game had no civilization for. How it got such a city
  is not observed (the political ids matched between host and guest for the first cities and three
  founded ones); the host's snapshot can change a city's owner to any political id (ChangeOwner).

## Fix
`civ.discovery_card`: AssetDiscovery_AddCard (0xE30CF0) with null data does nothing (logged); and a host
owner that names no civilization in the guest's game is not applied to the guest's city (logged once,
`owner_unknown=`). Symbols `SporeCoop::AssetDiscovery_AddCard`, `SporeCoop::CivDiscovery_Meet` (GOG 2024).

## Verification
* FAIL (`SPORE_FIX_OFF=civ.discovery_card`): `out/lab/20261103-131121`: the guest crashes at 0x00E2F7AF
  SetImageForWindow+0x21F (data 0xC).
* PASS: `out/lab/20261103-125816` (civ_discovery#1, #2): meeting an NPC civilization works, meeting an
  unknown political id makes no card (`cards_skipped=1`) and the game goes on.
* Not verified: the guest_cities owner rule (no host sends an unknown owner in the lab).

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-civ-crashes (civ.discovery_card).
- 2026-11-03: released in 0.7.4.
