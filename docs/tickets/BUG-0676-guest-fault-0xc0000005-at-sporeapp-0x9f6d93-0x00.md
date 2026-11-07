---
id: BUG-0676
title: Guest: fault 0xC0000005 at SporeApp+0x9F6D93 (0x00DF6D93, data 0x2C) while it loads a received Tribe world from the galaxy menu (mouse on a menu planet), then 22939 faults (chaos_tribe_awful)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-07
source: fix/w46-k3-bodies lab 20261107-113600 chaos_tribe_awful#1 (lane 7)
role: guest
versions: unreleased (rel/0.7.10 + fix/w46-k3-bodies 1cbce569, with SPORE_FIX_OFF=tribe.mirror_keep,drive.lead_keep)
fixed_in: unreleased
branch: fix/w46-rejoin-crash
commits:
tests: menu_hover, chaos_tribe_awful
signature: 0xC0000005 at 0x00DF6D93 SporeApp.exe+0x9F6D93 data=0x0000002C (main thread); calls 00927987 00F47234 00D486EB 00453A69 00927987 00DF7C2C 00DE825C 007D86D6 0081530F 007D8D6A
docs: docs/re/tribe.md#tribe-menu-rollover, docs/06-testing.md#testing-menu-rollover
related: BUG-0672
---

## Report
chaos_tribe_awful#1 of out/lab/20261107-113600 (lane 7, SPORE_NET=awful: 250 ms, 120 ms jitter, 10 % loss; SPORE_FIX_OFF=tribe.mirror_keep,drive.lead_keep, i.e. those two at 0.7.9's behaviour) failed `the games survived the chaos`: the chaos bot made the guest leave (`coop.leave`) and join again at 11:40:36-39; the guest 'did not get back into the host world' (no frames for 16 s while loading a world) and its log counts 22939 caught faults, the first 0xC0000005 at 0x00DF6D93 (SporeApp.exe+0x9F6D93, read of 0x2C: a null object's field) on the main thread. The guest's own log ends with the summary line of the session it left (faults 0) and the settings of the new start. The same test with the fixes on, at the same time on lane 6, passed (out/lab/20261107-113600-2 #1), and on rel/0.7.9 it passed once (lane 9).

How to reproduce: `SPORE_LANE=N python3 tools/tests/coop_chaos.py --fixture tribe --minutes 5.0 --seed 874162993 --churn 1` with SPORE_NET=awful (the replay line of the run).

## Root cause
Observed (Ghidra + runtime, docs/re/tribe.md#tribe-menu-rollover). Not the BUG-0672 class and not a rejoin: the fault
was the guest's first load after its cold start (11:36:45, 'loading Zorispe.spo'); the chaos bot's leave/join at
11:40:36 came while the guest was already stuck.
* 0xDF6D93 is the first instruction of GGE_PanelSetState 0xDF6D80 (`cmp [esi+0x2C]`, the panel null), called by the
  planet set's rollover 0xDF7BF0 (vt 0x5C) with 0 from the galaxy menu's exit 0xDE8160 (mode switch 0x7D8670 <
  SetActiveModeAt < LoadGameByName). 0xDF7BF0 rolls the old planet ([0x16A1344]+0xF8) out with 0xDF6D80(1) on its
  panel +0xB8 without a null check.
* +0xF8 points into the planet set's entry vector ([0x16A15FC]+0x08). The mod's load (world.cpp load_now) calls
  cStarManager::ReadStarsFromSaveDB before LoadGameByName; that sends 0x625D27D and the menu rebuilds its planets
  (0xDF95A0): all panels freed, +0xB8 zeroed, the vector resized (reallocated when larger) and refilled, +0xF8 kept.
  A cold guest's menu shows its empty slot (6 planets, room 6), the received slot has 7: with the mouse on a planet the
  rollover pointed into the freed block, the exit faulted.
* The storm: the exit had already released [GGE+0x184] and never finished; the mode switch never set the new mode, so
  the half torn-down menu stayed active and its frame (0xDF8700) faulted at 0xE01323 on the null +0x184 in every frame
  (22939 caught faults in the lab run; nothing logged after the first one of each place).

## Fix
* `game.menu_hover` (mods/coop/gamefixes.cpp, hook on SporeCoop::GGE_RebuildPlanets 0xDF95A0): before the rebuild the
  rollover planet is rolled out the game's way (vt 0x5C(0)); after it a rollover / picked pointer that names no entry
  of the new vector is dropped (log lines `game fixes: the galaxy menu rebuilds its planets with the mouse on planet ...
  rolled out first` / `... dropped`). Off (`coop.fix game.menu_hover off`): the game's code alone. Covers the game's
  own 0x625D27C path too.
* `kit.fix faultstorm` (SporeKit): one fault caught 300 times in 10 s logs `fault storm: <code> at <place> ... (BUG-0676)`
  once per place and writes one report per run (mark "kept faulting"), so a game stuck in such a loop leaves a report.
  Off: nothing (0.3.12).
* Test tools: `coop.menuhover [arm <i> | off]`, `coop.menuslot <GameN>`, twoinst.start(before_join=...).

## Verification
Test `coop_menu_hover.py` (suite menu_hover): a cold guest (menu: 6 planets, room 6) joins with its next menu rebuild
armed to put the mouse on planet 5; the received world has 7.
* FAIL (SPORE_FIX_OFF=game.menu_hover): out/lab/20261107-125914 (lane 6): "menu rollover 16272094 before the rebuild,
  16272094 after (no planet of the list; panel 00015E08 ...)", the fault in 0xDF6D80 (0x8102B2 UILayout::IsVisible+0x1A2:
  the freed block held a stale value instead of 0), calls 00DF7005 ... 00DF7C2C 00DE825C 007D86D6 0081530F as in the
  lab run, then 0xE01323 in every frame (16540 faults), the guest never got into the world; SporeKit logged
  `fault storm: 0xC0000005 at 0x00E01323 ... 454 times in 10 s` and wrote report-20261107-130002-i12.txt.
* PASS (fix on): out/lab/20261107-130424 (lane 7): "rolled out first (BUG-0676, 1)", 0 faults, the guest in the world.
* Not a repro: the same slot again (warm rejoin, 7 -> 7, no reallocation) does not fault with the fix off
  (20261107-125154, -125449); the first test version only showed that.

## History
- 2026-11-07: opened.
- 2026-11-07: opened by the K3 agent (fix/w46-k3-bodies) from a regression run; not analysed there.
- 2026-11-07: fix/w46-rejoin-crash: analysed (the rollover planet kept across the menu's planet rebuild), reproduced, fixed (game.menu_hover) and the storm made visible (kit.fix faultstorm).
