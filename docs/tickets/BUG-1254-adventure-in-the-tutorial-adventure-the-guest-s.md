---
id: BUG-1254
title: Adventure: in the tutorial adventure the guest's puppet appears as its old Creature-stage creature at the wrong size, and the guest's game stops seconds later
status: fixed
kind: crash
stage: Adventure
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in: 0.7.15.2
branch: fix/w53-advorn
commits: 3d2d26cb e5c33562
tests: adventure_tutorial
signature: 0xC0000005 at 0x00C0714E (Creature_ScenarioActSetup+0x7E) data 0x1, from cScenarioPlayMode::SetCurrentAct (0xF1E634) < Initialize
docs: docs/re/adventure.md#adventure-puppet-herd, docs/06-testing.md#testing-adventure-after-creature
related: BUG-0845, BUG-1260, BUG-1262
---

## Report
A players' video: both players start the tutorial adventure from the main menu. In the host's game the guest's puppet appears at the captain's feet with an [Adventure] plate: the old Creature-stage creature, the size of a rabbit. 3 s later the "is not responding (game minimized or frozen?)" plate, and the guest says its game crashed. Suggested causes: the tutorial adventure has a fixed resource key, so adventure_place() (modes.cpp) matched on both players without console commands and the mod spawned a puppet (docs/re/adventure.md assumed it never matches, BUG-0845); the kScenario adapter calls the plain creature_spawn with creations::kCreature (no captain), and the body scale sync is off outside the Creature stage; the guest's crash matches docs/re/space.md's note: cScenarioPlayMode::SetCurrentAct crashes when a mod puppet is in the scenario's object list.

## Root cause
* Observed (runtime, lab lane 9 and 7): the two games' copies of the tutorial adventure ("Adventure Town") have the same
  resource key (BF0CE0D0/408A0000; 8 of 11 earlier adventure_duo runs too), so the place ids match without console
  commands (the BUG-0845 assumption was wrong). After a Creature session each game has the partner's creation, and the
  Adventure adapter (the Creature spawn) made the puppet from it (065CBE70, scale 0.9-1.1 against the captains' 3.0) in
  a herd of its own: the players' "old creature at rabbit size at the captain's feet".
* Observed (runtime, both games): right after the puppet was made, Initialize -> SetCurrentAct faulted at 0xC0714E
  (data 1, esi = the puppet). Under Wine the game's handler caught it and the adventure never started (play state 0, no
  intro card); the players' guest crashed.
* Observed (disassembly): SetCurrentAct's pass 0xF1E5F0 calls 0xC070D0 for every creature not in the avatar's herd; it
  reads the act row of the creature's scenario object (cGameData +0x1C), unchecked: a creature the adventure did not
  place has none (row 0, read of address 1). The fork's crash of BUG-0845 is the same signature.
* Inferred: the guest's game "stopped ~3 s after" because its own Initialize ran right after it made the host's puppet.

## Fix
modes.cpp: the Adventure adapter has its own spawn / drive: the puppet is made in the avatar's herd (the act start
skips that herd), from this game's captain (the captains' species keys are made per game, the partner's look is
BUG-1262), at the captain's size (PlayerState body now carries the captain's scale in kScenario, x 1.15).
`coop.fix adventure.puppet_captain off`: the Creature stage's puppet (0.7.15).

## Verification
adventure_tutorial (Creature fixture, both to the galaxy menu, both start the tutorial adventure): FAIL with
`SPORE_FIX_OFF=adventure.puppet_captain` out/lab/20261118-131226 (the fault at 0xC0714E on both games, play state 0, the
puppets the Creature-stage creature at 1.08 / 0.93); PASS out/lab/20261118-131729 (the earlier build that looked the
partner's captain up by key), out/lab/20261118-132906 and 20261118-133328 (the final build; adventure_duo and adventure_duo_host_first passed with it; 20261118-132206: adventure_duo, adventure_save). Key frames: the FAIL run has no intro card and no goal panel; the
PASS runs show the intro card and the goal panel, each partner as a captain at the captain's size.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-advorn): reproduced (both games fault at 0xC0714E) and fixed (adventure.puppet_captain); new BUG-1260 (no main menu panel after an early load), BUG-1262 (the partner's captain look).
- 2026-11-18: released in 0.7.15.2.
