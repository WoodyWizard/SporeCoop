---
id: BUG-9187
title: Space launch: a guest never matches the host's retained Civ victory once the save has player identities or 3-4 founders
status: fixed
kind: bug
stage: space
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 28a8b539
tests: tools/qa_win (manual, TITAN host + 2 TITAN guests + the MSI, 2026-11-13)
signature:
docs:
related: BUG-9071 BUG-9135
switch: space.launch_all_seats
---

## Report
Found reading the phase 6 hand-off (2026-11-13, decision 3B) and reproduced live: four founders on a Space-ready Civ save (profile DuoQAJiffSpaceReady), the host in Space, the MSI joining from the menu. It asked for the host's retained Civ victory ("Loading your Civilization victory so you can design your spacecraft."), loaded it, was Space-ready on its own game, and then nothing: no "Open Civilization History to design your spacecraft", its own launch never started.

## Root cause
Observed in world.cpp: the launch offer (MSG_SPACE_LAUNCH Offer) wrote seats 0 and 1 (species, nation) only, and matching_launch_world compared it with the guest's whole saved roster (`local == offer.pair`): all eight seats and each seat's player identity. A CIV2 roster (identities, BUG-9135) or a third founder never matched, so the guest's Ready step (guest_launch_tick) never came. A guest already in the session launches through the end of the launch vote instead (coop.cpp MSG_VOTE), which is why the in-session players were not affected.

## Fix
space.launch_all_seats (on): the offer carries every founded seat after the first two (older guests stop reading after two and refuse the longer offer, as before), the reader takes the extra seats, and the guest compares species and nation of every seat (founders::Roster::same_seats), not the identities. Off: two seats, the whole roster.

## Verification
no-fail-run: manual, not a lab run. FAIL (switch off on host and MSI, the MSI restarted and joining from the menu, 17:19): the retained victory requested ("player 3 requested retained launch") and loaded, CivMode_SpaceReady true on the MSI, no Ready and no prompt in 2 minutes. PASS (switch on, the MSI restarted again, 17:22): the victory loaded and 1 s later "Open Civilization History to design your spacecraft and start your own flight tutorial."; its History, its own blank spaceship editor (96B24187), its ship, captain "MSICaptain" and "Welcome to Space!". In the same run the host, Late (seat 2) and Fourth (seat 1) launched through the vote: four founders in Space, each with an own ship; total faults 0 on all four games.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on feat/lineages-47 (decision 3B, Claude's pick).
